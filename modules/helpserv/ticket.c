/*
 * Copyright (c) 2005 William Pitcock <nenolod -at- nenolod.net>
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Allows requesting help for a account
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"helpserv/ticket", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.net>"
);

list_t *helpserv_cmdtree, *helpserv_helptree;

unsigned int ratelimit_count = 0;
time_t ratelimit_firsttime = 0;

static void account_drop_request(myuser_t *mu);
static void helpserv_cmd_request(sourceinfo_t *si, int parc, char *parv[]);
static void helpserv_cmd_list(sourceinfo_t *si, int parc, char *parv[]);
static void helpserv_cmd_close(sourceinfo_t *si, int parc, char *parv[]);
static void helpserv_cmd_cancel(sourceinfo_t *si, int parc, char *parv[]);

static void write_ticket_db(database_handle_t *db);
static void db_h_he(database_handle_t *db, const char *type);

command_t helpserv_request = { "REQUEST", N_("Request help from network staff."), AC_NONE, 1, helpserv_cmd_request };
command_t helpserv_list = { "LIST", N_("Lists users waiting for help."), PRIV_HELPER, 1, helpserv_cmd_list };
command_t helpserv_close = { "CLOSE", N_("Close a users' help request."), PRIV_HELPER, 2, helpserv_cmd_close };
command_t helpserv_cancel = { "CANCEL", N_("Cancel your own pending help request."), AC_NONE, 1, helpserv_cmd_cancel };

struct ticket_ {
	char *nick;
	time_t ticket_ts;
	char *creator;
	char *topic;
};

typedef struct ticket_ ticket_t;

list_t helpserv_reqlist;

void _modinit(module_t *m)
{
	if (!module_find_published("backend/opensex"))
	{
		slog(LG_INFO, "Module %s requires use of the OpenSEX database backend, refusing to load.", m->header->name);
		m->mflags = MODTYPE_FAIL;
		return;
	}

	MODULE_USE_SYMBOL(helpserv_cmdtree, "helpserv/main", "helpserv_cmdtree");
	MODULE_USE_SYMBOL(helpserv_helptree, "helpserv/main", "helpserv_helptree");

	hook_add_event("user_drop");
	hook_add_user_drop(account_drop_request);
	hook_add_db_write(write_ticket_db);

	db_register_type_handler("HE", db_h_he);

 	command_add(&helpserv_request, helpserv_cmdtree);
	command_add(&helpserv_list, helpserv_cmdtree);
	command_add(&helpserv_close, helpserv_cmdtree);
	command_add(&helpserv_cancel, helpserv_cmdtree);
	help_addentry(helpserv_helptree, "REQUEST", "help/helpserv/request", NULL);
	help_addentry(helpserv_helptree, "LIST", "help/helpserv/list", NULL);
	help_addentry(helpserv_helptree, "CLOSE", "help/helpserv/close", NULL);
	help_addentry(helpserv_helptree, "CANCEL", "help/helpserv/cancel", NULL);
}

void _moddeinit(void)
{
	hook_del_user_drop(account_drop_request);
	hook_del_db_write(write_ticket_db);

	db_unregister_type_handler("HE");

	command_delete(&helpserv_request, helpserv_cmdtree);
	command_delete(&helpserv_list, helpserv_cmdtree);
	command_delete(&helpserv_close, helpserv_cmdtree);
	command_delete(&helpserv_cancel, helpserv_cmdtree);
	help_delentry(helpserv_helptree, "REQUEST");
	help_delentry(helpserv_helptree, "LIST");
	help_delentry(helpserv_helptree, "CLOSE");
	help_delentry(helpserv_helptree, "CANCEL");
}

static void write_ticket_db(database_handle_t *db)
{
	node_t *n;

	LIST_FOREACH(n, helpserv_reqlist.head)
	{
		ticket_t *l = n->data;

		db_start_row(db, "HE");
		db_write_word(db, l->nick);
		db_write_time(db, l->ticket_ts);
		db_write_word(db, l->creator);
		db_write_str(db, l->topic);
		db_commit_row(db);
	}
}

static void db_h_he(database_handle_t *db, const char *type)
{
	const char *nick = db_sread_word(db);
	time_t ticket_ts = db_sread_time(db);
	const char *creator = db_sread_word(db);
	const char *topic = db_sread_str(db);

	ticket_t *l = smalloc(sizeof(ticket_t));
	l->nick = sstrdup(nick);
	l->ticket_ts = ticket_ts;
	l->creator = sstrdup(creator);
	l->topic = sstrdup(topic);
	node_add(l, node_create(), &helpserv_reqlist);
}

static void account_drop_request(myuser_t *mu)
{
        node_t *n;
        ticket_t *l;

        LIST_FOREACH(n, helpserv_reqlist.head)
        {
                l = n->data;
                if (!irccasecmp(l->nick, mu->name))
                {
                        slog(LG_REGISTER, "HELP:REQUEST:DROPACCOUNT: \2%s\2 \2%s\2", l->nick, l->topic);

                        node_del(n, &helpserv_reqlist);

                        free(l->nick);
                        free(l->creator);
                        free(l->topic);
                        free(l);

                        return;
                }
        }
}

/* REQUEST <topic> */
static void helpserv_cmd_request(sourceinfo_t *si, int parc, char *parv[])
{
	char *topic = parv[0];
	char *target;
	node_t *n;
	ticket_t *l;

	if (!topic)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "REQUEST");
		command_fail(si, fault_needmoreparams, _("Syntax: REQUEST <topic>"));
		return;
	}

	if (si->smu == NULL)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if ((unsigned int)(CURRTIME - ratelimit_firsttime) > config_options.ratelimit_period)
		ratelimit_count = 0, ratelimit_firsttime = CURRTIME;

	target = si->smu->name;

	/* search for it */
	LIST_FOREACH(n, helpserv_reqlist.head)
	{
		l = n->data;

		if (!irccasecmp(l->nick, target))
		{
			if (!strcmp(topic, l->topic))
			{
				command_success_nodata(si, _("You have already requested help about \2%s\2."), topic);
				return;
			}
			if (ratelimit_count > config_options.ratelimit_uses && !has_priv(si, PRIV_FLOOD))
			{
				command_fail(si, fault_toomany, _("The system is currently too busy to process your help request, please try again later."));
				slog(LG_INFO, "HELP:REQUEST:THROTTLED: %s", si->su->nick);
				return;
			}
			free(l->topic);
			l->topic = sstrdup(topic);
			l->ticket_ts = CURRTIME;;

			command_success_nodata(si, _("You have requested help about \2%s\2."), topic);
			logcommand(si, CMDLOG_REQUEST, "REQUEST: \2%s\2", topic);
			if (config_options.ratelimit_uses && config_options.ratelimit_period)
				ratelimit_count++;
			return;
		}
	}

	if (ratelimit_count > config_options.ratelimit_uses && !has_priv(si, PRIV_FLOOD))
	{
		command_fail(si, fault_toomany, _("The system is currently too busy to process your help request, please try again later."));
		slog(LG_INFO, "HELP:REQUEST:THROTTLED: %s", si->su->nick);
		return;
	}
	l = smalloc(sizeof(ticket_t));
	l->nick = sstrdup(target);
	l->ticket_ts = CURRTIME;;
	l->creator = sstrdup(get_source_name(si));
	l->topic = sstrdup(topic);

	n = node_create();
	node_add(l, n, &helpserv_reqlist);

	command_success_nodata(si, _("You have requested help about \2%s\2."), topic);
	logcommand(si, CMDLOG_REQUEST, "REQUEST: \2%s\2", topic);
	if (config_options.ratelimit_uses && config_options.ratelimit_period)
		ratelimit_count++;
	return;
}

/* CLOSE <nick> */
static void helpserv_cmd_close(sourceinfo_t *si, int parc, char *parv[])
{
	char *nick = parv[0];
	user_t *u;
	ticket_t *l;
	node_t *n;

	if (!nick)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CLOSE");
		command_fail(si, fault_needmoreparams, _("Syntax: CLOSE <nick>"));
		return;
	}


	LIST_FOREACH(n, helpserv_reqlist.head)
	{
		l = n->data;
		if (!irccasecmp(l->nick, nick))
		{
			if ((u = user_find_named(nick)) != NULL)
				notice(si->service->nick, u->nick, "[auto notice] Your help request has been closed.");
			/* VHOSTNICK command below will generate snoop */
			logcommand(si, CMDLOG_REQUEST, "CLOSE: Help for \2%s\2 about \2%s\2", nick, l->topic);

			node_del(n, &helpserv_reqlist);

			free(l->nick);
			free(l->creator);
			free(l->topic);
			free(l);

			return;
		}
	}
	command_success_nodata(si, _("Nick \2%s\2 not found in help request database."), nick);
}

/* LIST */
static void helpserv_cmd_list(sourceinfo_t *si, int parc, char *parv[])
{
	ticket_t *l;
	node_t *n;
	int x = 0;
	char buf[BUFSIZE];
	struct tm tm;

	LIST_FOREACH(n, helpserv_reqlist.head)
	{
		l = n->data;
		x++;

		tm = *localtime(&l->ticket_ts);
		strftime(buf, BUFSIZE, "%b %d %T %Y %Z", &tm);
		command_success_nodata(si, "#%d Nick:\2%s\2, topic:\2%s\2 (%s - %s)",
			x, l->nick, l->topic, l->creator, buf);
	}
	command_success_nodata(si, "End of list.");
	logcommand(si, CMDLOG_GET, "LIST");
}

/* CANCEL */
static void helpserv_cmd_cancel(sourceinfo_t *si, int parc, char *parv[])
{
        ticket_t *l;
        node_t *n;
        char *target;

        if (si->smu == NULL)
        {
                command_fail(si, fault_noprivs, _("You are not logged in."));
                return;
        }

        target = si->smu->name;

        LIST_FOREACH(n, helpserv_reqlist.head)
        {
                l = n->data;

                if (!irccasecmp(l->nick, target))
                {
                        node_del(n, &helpserv_reqlist);

                        free(l->nick);
                        free(l->creator);
                        free(l->topic);
                        free(l);

                        command_success_nodata(si, "Your help request has been cancelled.");

                        logcommand(si, CMDLOG_REQUEST, "CANCEL");
                        return;
                }
        }
        command_fail(si, fault_badparams, _("You do not have a help request to cancel."));
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
