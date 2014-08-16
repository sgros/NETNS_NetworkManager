/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright 2010 - 2014 Red Hat, Inc.
 *
 */

#include <glib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#include <NetworkManager.h>
#include "nm-glib-compat.h"
#include "nm-test-utils.h"

#include "common.h"

/*******************************************************************/

enum {
	SECRET_REQUESTED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef NMSecretAgent TestSecretAgent;
typedef NMSecretAgentClass TestSecretAgentClass;

GType test_secret_agent_get_type (void);
G_DEFINE_TYPE (TestSecretAgent, test_secret_agent, NM_TYPE_SECRET_AGENT)

static void
test_secret_agent_init (TestSecretAgent *agent)
{
}

static void
test_secret_agent_get_secrets (NMSecretAgent                 *agent,
                               NMConnection                  *connection,
                               const char                    *connection_path,
                               const char                    *setting_name,
                               const char                   **hints,
                               NMSecretAgentGetSecretsFlags   flags,
                               NMSecretAgentGetSecretsFunc    callback,
                               gpointer                       callback_data)
{
	NMSettingWirelessSecurity *s_wsec;
	GVariant *secrets = NULL;
	GVariantBuilder secrets_builder, setting_builder;
	char *secret = NULL;
	GError *error = NULL;

	g_assert_cmpstr (setting_name, ==, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME);

	s_wsec = nm_connection_get_setting_wireless_security (connection);
	g_assert (s_wsec);
	g_assert_cmpstr (nm_setting_wireless_security_get_key_mgmt (s_wsec), ==, "wpa-psk");
	g_assert_cmpstr (nm_setting_wireless_security_get_psk (s_wsec), ==, NULL);

	g_signal_emit (agent, signals[SECRET_REQUESTED], 0,
	               connection,
	               connection_path,
	               NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	               NM_SETTING_WIRELESS_SECURITY_PSK,
	               &secret);

	if (!secret) {
		error = g_error_new (NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_NO_SECRETS,
		                     "No secrets");
		goto done;
	}

	if (!strcmp (secret, "CANCEL")) {
		error = g_error_new (NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_USER_CANCELED,
		                     "User canceled");
		goto done;
	}

	g_variant_builder_init (&setting_builder, NM_VARIANT_TYPE_SETTING);
	g_variant_builder_add (&setting_builder, "{sv}",
	                       NM_SETTING_WIRELESS_SECURITY_PSK,
	                       g_variant_new_string (secret));

	g_variant_builder_init (&secrets_builder, NM_VARIANT_TYPE_CONNECTION);
	g_variant_builder_add (&secrets_builder, "{sa{sv}}",
	                       setting_name,
	                       &setting_builder);
	secrets = g_variant_ref_sink (g_variant_builder_end (&secrets_builder));

done:
	callback (agent, connection, secrets, error, callback_data);
	g_clear_error (&error);
	g_clear_pointer (&secrets, g_variant_unref);
	g_free (secret);
}

static void
test_secret_agent_cancel_get_secrets (NMSecretAgent *agent,
                                      const gchar   *connection_path,
                                      const gchar   *setting_name)
{
	g_assert_not_reached ();
}

static void
test_secret_agent_save_secrets (NMSecretAgent                *agent,
                               NMConnection                 *connection,
                               const gchar                  *connection_path,
                               NMSecretAgentSaveSecretsFunc  callback,
                               gpointer                      callback_data)
{
	g_assert_not_reached ();
}

static void
test_secret_agent_delete_secrets (NMSecretAgent                  *agent,
                                 NMConnection                   *connection,
                                 const gchar                    *connection_path,
                                 NMSecretAgentDeleteSecretsFunc  callback,
                                 gpointer                        callback_data)
{
	g_assert_not_reached ();
}

static void
test_secret_agent_class_init (TestSecretAgentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMSecretAgentClass *agent_class = NM_SECRET_AGENT_CLASS (klass);

	agent_class->get_secrets = test_secret_agent_get_secrets;
	agent_class->cancel_get_secrets = test_secret_agent_cancel_get_secrets;
	agent_class->save_secrets = test_secret_agent_save_secrets;
	agent_class->delete_secrets = test_secret_agent_delete_secrets;

	signals[SECRET_REQUESTED] =
		g_signal_new ("secret-requested",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_STRING, 4,
		              NM_TYPE_CONNECTION,
		              G_TYPE_STRING,
		              G_TYPE_STRING,
		              G_TYPE_STRING);

}

static NMSecretAgent *
test_secret_agent_new (void)
{
	NMSecretAgent *agent;
	GError *error = NULL;

	agent = g_initable_new (test_secret_agent_get_type (), NULL, &error,
	                        NM_SECRET_AGENT_IDENTIFIER, "test-secret-agent",
	                        NM_SECRET_AGENT_AUTO_REGISTER, FALSE,
	                        NULL);
	g_assert_no_error (error);

	return agent;
}

/*******************************************************************/

typedef struct {
	NMTestServiceInfo *sinfo;
	NMClient *client;
	NMRemoteSettings *settings;

	NMSecretAgent *agent;
	NMDevice *device;
	NMConnection *connection;

	GMainLoop *loop;
	guint timeout_id;

	char *ifname;
	char *con_id;
	char *devpath;

	int secrets_requested;
} TestSecretAgentData;

static gboolean
timeout_assert (gpointer user_data)
{
	g_assert_not_reached ();
}

static void
device_added_cb (NMClient *c,
                 NMDevice *device,
                 gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert (device);
	g_assert_cmpstr (nm_device_get_iface (device), ==, sadata->ifname);

	sadata->device = device;
	g_main_loop_quit (sadata->loop);
}

static void
connection_added_cb (NMRemoteSettings *s,
                     NMRemoteConnection *connection,
                     GError *error,
                     gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert_no_error (error);
	g_assert_cmpstr (nm_connection_get_id (NM_CONNECTION (connection)), ==, sadata->con_id);

	sadata->connection = NM_CONNECTION (connection);
	g_main_loop_quit (sadata->loop);
}

static void
register_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;
	GError *error = NULL;

	nm_secret_agent_register_finish (sadata->agent, result, &error);
	g_assert_no_error (error);
	g_assert (nm_secret_agent_get_registered (sadata->agent));

	g_main_loop_quit (sadata->loop);
}

#define TEST_CON_ID_PREFIX "test-secret-agent"

static void
test_setup (TestSecretAgentData *sadata, gconstpointer test_data)
{
	static int counter = 0;
	const char *agent_notes = test_data;
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWireless *s_wireless;
	GBytes *ssid;
	NMSetting *s_wsec;
	gboolean success;
	GError *error = NULL;
	GVariant *ret;
	gulong handler;

	sadata->sinfo = nm_test_service_init ();
	sadata->client = nm_client_new (NULL, &error);
	g_assert_no_error (error);
	sadata->settings = nm_remote_settings_new (NULL, &error);
	g_assert_no_error (error);

	sadata->loop = g_main_loop_new (NULL, FALSE);
	sadata->timeout_id = g_timeout_add_seconds (5, timeout_assert, NULL);

	sadata->ifname = g_strdup_printf ("wlan%d", counter);
	sadata->con_id = g_strdup_printf ("%s-%d", TEST_CON_ID_PREFIX, counter);
	counter++;

	/* Create the device */
	ret = g_dbus_proxy_call_sync (sadata->sinfo->proxy,
	                              "AddWifiDevice",
	                              g_variant_new ("(s)", sadata->ifname),
	                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
	                              3000,
	                              NULL,
	                              &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpstr (g_variant_get_type_string (ret), ==, "(o)");
	g_variant_get (ret, "(o)", &sadata->devpath);
	g_variant_unref (ret);

	handler = g_signal_connect (sadata->client, "device-added",
	                            G_CALLBACK (device_added_cb), sadata);
	g_main_loop_run (sadata->loop);
	g_signal_handler_disconnect (sadata->client, handler);
	g_assert (sadata->device);

	/* Create the connection */
	connection = nmtst_create_minimal_connection (sadata->con_id, NULL, NM_SETTING_WIRELESS_SETTING_NAME, &s_con);
	g_object_set (s_con,
	              NM_SETTING_CONNECTION_INTERFACE_NAME, sadata->ifname,
	              NULL);

	s_wireless = nm_connection_get_setting_wireless (connection);
	ssid = g_bytes_new ("foo", 3);
	g_object_set (s_wireless,
	              NM_SETTING_WIRELESS_SSID, ssid,
	              NULL);
	g_bytes_unref (ssid);

	s_wsec = g_object_new (NM_TYPE_SETTING_WIRELESS_SECURITY,
	                       NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
	                       NULL);
	nm_connection_add_setting (connection, s_wsec);

	success = nm_remote_settings_add_connection (sadata->settings,
	                                             connection,
	                                             connection_added_cb,
	                                             sadata);
	g_assert (success == TRUE);
	g_object_unref (connection);

	g_main_loop_run (sadata->loop);
	g_assert (sadata->connection);

	if (agent_notes) {
		sadata->agent = test_secret_agent_new ();

		if (!strcmp (agent_notes, "sync")) {
			nm_secret_agent_register (sadata->agent, NULL, &error);
			g_assert_no_error (error);
			g_assert (nm_secret_agent_get_registered (sadata->agent));
		} else {
			nm_secret_agent_register_async (sadata->agent, NULL,
			                                register_cb, sadata);
			g_main_loop_run (sadata->loop);
		}
	}
}

static void
test_cleanup (TestSecretAgentData *sadata, gconstpointer test_data)
{
	GVariant *ret;
	GError *error = NULL;

	if (sadata->agent) {
		if (nm_secret_agent_get_registered (sadata->agent)) {
			nm_secret_agent_unregister (sadata->agent, NULL, &error);
			g_assert_no_error (error);
		}
		g_object_unref (sadata->agent);
	}

	g_object_unref (sadata->client);
	g_object_unref (sadata->settings);

	ret = g_dbus_proxy_call_sync (sadata->sinfo->proxy,
	                              "RemoveDevice",
	                              g_variant_new ("(s)", sadata->devpath),
	                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
	                              3000,
	                              NULL,
	                              &error);
	g_assert_no_error (error);
	g_variant_unref (ret);

	nm_test_service_cleanup (sadata->sinfo);

	g_source_remove (sadata->timeout_id);
	g_main_loop_unref (sadata->loop);

	g_free (sadata->ifname);
	g_free (sadata->con_id);
	g_free (sadata->devpath);
}

/*******************************************************************/

static void
connection_activated_none_cb (NMClient *c,
                              NMActiveConnection *ac,
                              GError *error,
                              gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==, "No secret agent available");

	g_main_loop_quit (sadata->loop);
}

static void
test_secret_agent_none (TestSecretAgentData *sadata, gconstpointer test_data)
{
	nm_client_activate_connection (sadata->client,
	                               sadata->connection,
	                               sadata->device,
	                               NULL,
	                               connection_activated_none_cb,
	                               sadata);
	g_main_loop_run (sadata->loop);
}

/*******************************************************************/

static char *
secrets_requested_no_secrets_cb (TestSecretAgent *agent,
                                 NMConnection *connection,
                                 const char *connection_path,
                                 const char *setting_name,
                                 const char *secret_name,
                                 gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert_cmpstr (connection_path, ==, nm_connection_get_path (sadata->connection));
	sadata->secrets_requested++;

	return NULL;
}

static void
connection_activated_no_secrets_cb (NMClient *c,
                                    NMActiveConnection *ac,
                                    GError *error,
                                    gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==, "No secrets provided");

	g_main_loop_quit (sadata->loop);
}

static void
test_secret_agent_no_secrets (TestSecretAgentData *sadata, gconstpointer test_data)
{
	g_signal_connect (sadata->agent, "secret-requested",
	                  G_CALLBACK (secrets_requested_no_secrets_cb),
	                  sadata);

	nm_client_activate_connection (sadata->client,
	                               sadata->connection,
	                               sadata->device,
	                               NULL,
	                               connection_activated_no_secrets_cb,
	                               sadata);
	g_main_loop_run (sadata->loop);

	g_assert_cmpint (sadata->secrets_requested, ==, 1);
}

/*******************************************************************/

static void
connection_activated_cancel_cb (NMClient *c,
                                NMActiveConnection *ac,
                                GError *error,
                                gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==, "User canceled");

	g_main_loop_quit (sadata->loop);
}

static char *
secrets_requested_cancel_cb (TestSecretAgent *agent,
                             NMConnection *connection,
                             const char *connection_path,
                             const char *setting_name,
                             const char *secret_name,
                             gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert_cmpstr (connection_path, ==, nm_connection_get_path (sadata->connection));
	sadata->secrets_requested++;

	return g_strdup ("CANCEL");
}

static void
test_secret_agent_cancel (TestSecretAgentData *sadata, gconstpointer test_data)
{
	g_signal_connect (sadata->agent, "secret-requested",
	                  G_CALLBACK (secrets_requested_cancel_cb),
	                  sadata);

	nm_client_activate_connection (sadata->client,
	                               sadata->connection,
	                               sadata->device,
	                               NULL,
	                               connection_activated_cancel_cb,
	                               sadata);
	g_main_loop_run (sadata->loop);

	g_assert_cmpint (sadata->secrets_requested, ==, 1);
}

/*******************************************************************/

static void
connection_activated_good_cb (NMClient *c,
                              NMActiveConnection *ac,
                              GError *error,
                              gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	/* test-networkmanager-service.py doesn't implement activation, but
	 * we should at least get as far as the error telling us that (which the
	 * other tests won't get to).
	 */
	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==, "Not yet implemented");

	g_main_loop_quit (sadata->loop);
}

static char *
secrets_requested_good_cb (TestSecretAgent *agent,
                           NMConnection *connection,
                           const char *connection_path,
                           const char *setting_name,
                           const char *secret_name,
                           gpointer user_data)
{
	TestSecretAgentData *sadata = user_data;

	g_assert_cmpstr (connection_path, ==, nm_connection_get_path (sadata->connection));
	sadata->secrets_requested++;

	return g_strdup ("password");
}

static void
test_secret_agent_good (TestSecretAgentData *sadata, gconstpointer test_data)
{
	g_signal_connect (sadata->agent, "secret-requested",
	                  G_CALLBACK (secrets_requested_good_cb),
	                  sadata);

	nm_client_activate_connection (sadata->client,
	                               sadata->connection,
	                               sadata->device,
	                               NULL,
	                               connection_activated_good_cb,
	                               sadata);
	g_main_loop_run (sadata->loop);

	g_assert_cmpint (sadata->secrets_requested, ==, 1);
}


static void
async_init_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
	GMainLoop *loop = user_data;
	GError *error = NULL;
	GObject *agent;

	agent = g_async_initable_new_finish (G_ASYNC_INITABLE (object), result, &error);
	g_assert_error (error, NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_INTERNAL_ERROR);
	g_assert (agent == NULL);
	g_clear_error (&error);

	g_main_loop_quit (loop);
}

static void
test_secret_agent_nm_not_running (void)
{
	NMSecretAgent *agent;
	GMainLoop *loop;
	GError *error = NULL;

	agent = g_initable_new (test_secret_agent_get_type (), NULL, &error,
	                        NM_SECRET_AGENT_IDENTIFIER, "test-secret-agent",
	                        NULL);
	g_assert_error (error, NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_INTERNAL_ERROR);
	g_assert (agent == NULL);
	g_clear_error (&error);

	loop = g_main_loop_new (NULL, FALSE);
	g_async_initable_new_async (test_secret_agent_get_type (),
	                            G_PRIORITY_DEFAULT,
	                            NULL, async_init_cb, loop,
	                            NM_SECRET_AGENT_IDENTIFIER, "test-secret-agent",
	                            NULL);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);
}


static void
registered_changed (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	GMainLoop *loop = user_data;

	g_main_loop_quit (loop);
}

static void
test_secret_agent_auto_register (void)
{
	NMTestServiceInfo *sinfo;
	NMSecretAgent *agent;
	GMainLoop *loop;
	GError *error = NULL;

	sinfo = nm_test_service_init ();
	loop = g_main_loop_new (NULL, FALSE);

	agent = test_secret_agent_new ();
	g_object_set (agent,
	              NM_SECRET_AGENT_AUTO_REGISTER, TRUE,
	              NULL);
	g_signal_connect (agent, "notify::" NM_SECRET_AGENT_REGISTERED,
	                  G_CALLBACK (registered_changed), loop);

	g_assert (!nm_secret_agent_get_registered (agent));
	nm_secret_agent_register (agent, NULL, &error);
	g_assert_no_error (error);
	g_assert (nm_secret_agent_get_registered (agent));

	/* Shut down test service */
	nm_test_service_cleanup (sinfo);
	g_main_loop_run (loop);
	g_assert (!nm_secret_agent_get_registered (agent));

	/* Restart test service */
	sinfo = nm_test_service_init ();
	g_main_loop_run (loop);
	g_assert (nm_secret_agent_get_registered (agent));

	/* Shut down test service again */
	nm_test_service_cleanup (sinfo);
	g_main_loop_run (loop);
	g_assert (!nm_secret_agent_get_registered (agent));

	g_object_unref (agent);
	g_main_loop_unref (loop);
}

/*******************************************************************/

int
main (int argc, char **argv)
{
	int ret;

	g_setenv ("LIBNM_USE_SESSION_BUS", "1", TRUE);

#if !GLIB_CHECK_VERSION (2, 35, 0)
	g_type_init ();
#endif

	g_test_init (&argc, &argv, NULL);

	g_test_add ("/libnm/secret-agent/none", TestSecretAgentData, NULL,
	            test_setup, test_secret_agent_none, test_cleanup);
	g_test_add ("/libnm/secret-agent/no-secrets", TestSecretAgentData, "sync",
	            test_setup, test_secret_agent_no_secrets, test_cleanup);
	g_test_add ("/libnm/secret-agent/cancel", TestSecretAgentData, "async",
	            test_setup, test_secret_agent_cancel, test_cleanup);
	g_test_add ("/libnm/secret-agent/good", TestSecretAgentData, "async",
	            test_setup, test_secret_agent_good, test_cleanup);
	g_test_add_func ("/libnm/secret-agent/nm-not-running", test_secret_agent_nm_not_running);
	g_test_add_func ("/libnm/secret-agent/auto-register", test_secret_agent_auto_register);

	ret = g_test_run ();

	return ret;
}

