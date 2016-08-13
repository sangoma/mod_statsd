/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 * Portions created by Seventh Signal Ltd. & Co. KG and its employees are Copyright (C)
 * Seventh Signal Ltd. & Co. KG, All Rights Reserverd.
 *
 * Contributor(s):
 * Joao Mesquita <jmesquita@sangoma.com>
 *
 * mod_statsd.c -- Send metrics to statsd server
 *
 */
#include <switch.h>
#include "statsd-client.h"

static struct {
	switch_memory_pool_t *pool;
	switch_mutex_t *mutex;
	int port;
	statsd_link* link;
	char *host;
	char *namespace;
	int shutdown;
} globals;

SWITCH_MODULE_LOAD_FUNCTION(mod_statsd_load);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_statsd_runtime);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_statsd_shutdown);
SWITCH_MODULE_DEFINITION(mod_statsd, mod_statsd_load, mod_statsd_shutdown, mod_statsd_runtime);

static switch_status_t load_config(switch_memory_pool_t *pool)
{
	char *cf = "statsd.conf";
	switch_xml_t cfg, xml, settings, param;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	memset(&globals, 0, sizeof(globals));

	globals.pool = pool;
	switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, globals.pool);

	if ((xml = switch_xml_open_cfg(cf, &cfg, NULL))) {

		if ((settings = switch_xml_child(cfg, "settings"))) {
			for (param = switch_xml_child(settings, "param"); param; param = param->next) {
				char *var = (char *) switch_xml_attr_soft(param, "name");
				char *val = (char *) switch_xml_attr_soft(param, "value");
				if (!strcasecmp(var, "namespace")) {
					globals.namespace = val;
				} else if (!strcasecmp(var, "host")) {
					globals.host = val;
				} else if (!strcasecmp(var, "port")) {
					globals.port = atoi(val);
				}
			}
		}
		switch_xml_free(xml);
	}

	if (zstr(globals.host)) {
		globals.host = switch_core_strdup(pool, "127.0.0.1");
	}

	if (!globals.port) {
		globals.port = 8125;
	}

	return status;
}

static int sql_count_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	uint32_t *count = (uint32_t *) pArg;
	*count = atoi(argv[0]);
	return 0;
}

/**
 * Polls for all metrics
 */
SWITCH_MODULE_RUNTIME_FUNCTION(mod_statsd_runtime)
{
	uint32_t int_val;
	switch_cache_db_handle_t *dbh;
	char sql[1024] = "";

	while(globals.shutdown == 0) {
		switch_mutex_lock(globals.mutex);

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Polling for metrics\n");

		statsd_gauge(globals.link, "sessions_since_startup", switch_core_session_id() - 1);
		statsd_gauge(globals.link, "sessions_count", switch_core_session_count());
		switch_core_session_ctl(SCSC_SESSIONS_PEAK, &int_val);
		statsd_gauge(globals.link, "sessions_count_peak", int_val);
		switch_core_session_ctl(SCSC_LAST_SPS, &int_val);
		statsd_gauge(globals.link, "sessions_per_second", int_val);
		switch_core_session_ctl(SCSC_SPS_PEAK, &int_val);
		statsd_gauge(globals.link, "sessions_per_second_peak", int_val);
		switch_core_session_ctl(SCSC_SESSIONS_PEAK_FIVEMIN, &int_val);
		statsd_gauge(globals.link, "sessions_per_second_5min", int_val);


		if (switch_core_db_handle(&dbh) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "No database to output calls or channels.\n");
		} else {
			sprintf(sql, "SELECT COUNT(*) FROM basic_calls WHERE hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "call_count", int_val);

			sprintf(sql, "select count(*) from channels where hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "channel_count", int_val);

			sprintf(sql, "select count(*) from registrations where hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "registration_count", int_val);
			switch_cache_db_release_db_handle(&dbh);
		}

		switch_mutex_unlock(globals.mutex);
		switch_sleep(1000*1000); // 1s
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Runtime thread is done\n");
	return SWITCH_STATUS_TERM;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_statsd_load)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	switch_management_interface_t *management_interface;

	load_config(pool);

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	management_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_MANAGEMENT_INTERFACE);
	management_interface->relative_oid = "2000";

	if (zstr(globals.namespace)) {
		globals.link = statsd_init(globals.host, globals.port);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending stats to %s:%d\n", globals.host, globals.port);
	} else {
		globals.link = statsd_init_with_namespace(globals.host, globals.port, globals.namespace);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
				"Sending stats to %s:%d with namespace %s\n", globals.host, globals.port, globals.namespace);
	}

	return status;
}


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_statsd_shutdown)
{
	switch_mutex_lock(globals.mutex);
	statsd_finalize(globals.link);
	globals.shutdown = 1;
	switch_mutex_unlock(globals.mutex);

	switch_mutex_destroy(globals.mutex);

	return SWITCH_STATUS_SUCCESS;
}



/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
