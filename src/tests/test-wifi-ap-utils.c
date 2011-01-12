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
 * Copyright (C) 2011 Red Hat, Inc.
 *
 */

#include <glib.h>
#include <string.h>

#include "nm-wifi-ap-utils.h"
#include "nm-dbus-glib-types.h"

#include "nm-setting-connection.h"
#include "nm-setting-wireless.h"
#include "nm-setting-wireless-security.h"
#include "nm-setting-8021x.h"

#define DEBUG 1

/*******************************************/

#define COMPARE(src, expected, success, error, edomain, ecode) \
{ \
	if (expected) { \
		if (!success) { \
			g_assert (error != NULL); \
			g_warning ("Failed to complete connection: (%d) %s", error->code, error->message); \
		} \
		g_assert (success == TRUE); \
		g_assert (error == NULL); \
\
		success = nm_connection_compare (src, expected, NM_SETTING_COMPARE_FLAG_EXACT); \
		if (success == FALSE && DEBUG) { \
			g_message ("\n- COMPLETED ---------------------------------\n"); \
			nm_connection_dump (src); \
			g_message ("+ EXPECTED ++++++++++++++++++++++++++++++++++++\n"); \
			nm_connection_dump (expected); \
			g_message ("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"); \
		} \
		g_assert (success == TRUE); \
	} else { \
		if (success) { \
			g_message ("\n- COMPLETED ---------------------------------\n"); \
			nm_connection_dump (src); \
		} \
		g_assert (success == FALSE); \
		g_assert_error (error, edomain, ecode); \
	} \
 \
	g_clear_error (&error); \
}

static gboolean
complete_connection (const char *ssid,
                     const guint8 bssid[ETH_ALEN],
                     NM80211Mode mode,
                     guint32 flags,
                     guint32 wpa_flags,
                     guint32 rsn_flags,
                     gboolean lock_bssid,
                     NMConnection *src,
                     GError **error)
{
	GByteArray *tmp;
	gboolean success;

	tmp = g_byte_array_sized_new (strlen (ssid));
	g_byte_array_append (tmp, (const guint8 *) ssid, strlen (ssid));

	success = nm_ap_utils_complete_connection (tmp,
	                                           bssid,
	                                           mode,
	                                           flags,
	                                           wpa_flags,
	                                           rsn_flags,
	                                           src,
	                                           lock_bssid,
	                                           error);
	g_byte_array_free (tmp, TRUE);
	return success;
}

typedef struct {
	const char *key;
	const char *str;
	guint32     uint;
} KeyData;

static void
set_items (NMSetting *setting, const KeyData *items)
{
	const KeyData *item;
	GParamSpec *pspec;
	GByteArray *tmp;

	for (item = items; item && item->key; item++) {
		g_assert (item->key);
		pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (setting), item->key);
		g_assert (pspec);

		if (pspec->value_type == G_TYPE_STRING) {
			g_assert (item->uint == 0);
			if (item->str)
				g_object_set (G_OBJECT (setting), item->key, item->str, NULL);
		} else if (pspec->value_type == G_TYPE_UINT) {
			g_assert (item->str == NULL);
			g_object_set (G_OBJECT (setting), item->key, item->uint, NULL);
		} else if (pspec->value_type == G_TYPE_INT) {
			gint foo = (gint) item->uint;

			g_assert (item->str == NULL);
			g_object_set (G_OBJECT (setting), item->key, foo, NULL);
		} else if (pspec->value_type == G_TYPE_BOOLEAN) {
			gboolean foo = !! (item->uint);

			g_assert (item->str == NULL);
			g_object_set (G_OBJECT (setting), item->key, foo, NULL);
		} else if (pspec->value_type == DBUS_TYPE_G_UCHAR_ARRAY) {
			g_assert (item->str);
			tmp = g_byte_array_sized_new (strlen (item->str));
			g_byte_array_append (tmp, (const guint8 *) item->str, strlen (item->str));
			g_object_set (G_OBJECT (setting), item->key, tmp, NULL);
			g_byte_array_free (tmp, TRUE);
		} else {
			/* Special types, check based on property name */
			if (!strcmp (item->key, NM_SETTING_WIRELESS_SECURITY_PROTO))
				nm_setting_wireless_security_add_proto (NM_SETTING_WIRELESS_SECURITY (setting), item->str);
			else if (!strcmp (item->key, NM_SETTING_WIRELESS_SECURITY_PAIRWISE))
				nm_setting_wireless_security_add_pairwise (NM_SETTING_WIRELESS_SECURITY (setting), item->str);
			else if (!strcmp (item->key, NM_SETTING_WIRELESS_SECURITY_GROUP))
				nm_setting_wireless_security_add_group (NM_SETTING_WIRELESS_SECURITY (setting), item->str);
			else if (!strcmp (item->key, NM_SETTING_802_1X_EAP))
				nm_setting_802_1x_add_eap_method (NM_SETTING_802_1X (setting), item->str);
		}
	}
}

static NMSettingWireless *
fill_wifi_empty (NMConnection *connection)
{
	NMSettingWireless *s_wifi;

	s_wifi = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
	if (!s_wifi) {
		s_wifi = (NMSettingWireless *) nm_setting_wireless_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_wifi));
	}
	return s_wifi;
}

static NMSettingWireless *
fill_wifi (NMConnection *connection, const KeyData items[])
{
	NMSettingWireless *s_wifi;

	s_wifi = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
	if (!s_wifi) {
		s_wifi = (NMSettingWireless *) nm_setting_wireless_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_wifi));
	}

	set_items (NM_SETTING (s_wifi), items);
	return s_wifi;
}

static NMSettingWirelessSecurity *
fill_wsec (NMConnection *connection, const KeyData items[])
{
	NMSettingWirelessSecurity *s_wsec;

	s_wsec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
	if (!s_wsec) {
		s_wsec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_wsec));
	}

	set_items (NM_SETTING (s_wsec), items);
	return s_wsec;
}

static NMSetting8021x *
fill_8021x (NMConnection *connection, const KeyData items[])
{
	NMSetting8021x *s_8021x;

	s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
	if (!s_8021x) {
		s_8021x = (NMSetting8021x *) nm_setting_802_1x_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_8021x));
	}

	set_items (NM_SETTING (s_8021x), items);
	return s_8021x;
}

static NMConnection *
create_basic (const char *ssid,
              const guint8 *bssid,
              NM80211Mode mode,
              gboolean set_security)
{
	NMConnection *connection;
	NMSettingWireless *s_wifi = NULL;
	GByteArray *tmp;

	connection = nm_connection_new ();

	s_wifi = (NMSettingWireless *) nm_setting_wireless_new ();
	nm_connection_add_setting (connection, NM_SETTING (s_wifi));

	/* SSID */
	tmp = g_byte_array_sized_new (strlen (ssid));
	g_byte_array_append (tmp, (const guint8 *) ssid, strlen (ssid));
	g_object_set (G_OBJECT (s_wifi), NM_SETTING_WIRELESS_SSID, tmp, NULL);
	g_byte_array_free (tmp, TRUE);

	/* BSSID */
	if (bssid) {
		tmp = g_byte_array_sized_new (ETH_ALEN);
		g_byte_array_append (tmp, bssid, ETH_ALEN);
		g_object_set (G_OBJECT (s_wifi), NM_SETTING_WIRELESS_BSSID, tmp, NULL);
		g_byte_array_free (tmp, TRUE);
	}

	if (mode == NM_802_11_MODE_INFRA)
		g_object_set (G_OBJECT (s_wifi), NM_SETTING_WIRELESS_MODE, "infrastructure", NULL);
	else if (mode == NM_802_11_MODE_ADHOC)
		g_object_set (G_OBJECT (s_wifi), NM_SETTING_WIRELESS_MODE, "adhoc", NULL);
	else
		g_assert_not_reached ();

	if (set_security)
		g_object_set (G_OBJECT (s_wifi), NM_SETTING_WIRELESS_SEC, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL);

	return connection;
}

/*******************************************/

static void
test_lock_bssid (void)
{
	NMConnection *src, *expected;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *ssid = "blahblah";
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_NONE,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               TRUE,
	                               src, &error);
	expected = create_basic (ssid, bssid, NM_802_11_MODE_INFRA, FALSE);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
	g_object_unref (expected);
}

/*******************************************/

static void
test_open_ap_empty_connection (void)
{
	NMConnection *src, *expected;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *ssid = "blahblah";
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_NONE,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, FALSE);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
	g_object_unref (expected);
}

/*******************************************/

static void
test_open_ap_leap_connection_1 (gboolean add_wifi)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = { { NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME, "Bill Smith", 0 }, { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	if (add_wifi)
		fill_wifi_empty (src);
	fill_wsec (src, src_wsec);

	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_NONE,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_open_ap_leap_connection_2 (void)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = { { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 }, { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);

	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_NONE,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_open_ap_wep_connection (gboolean add_wifi)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_WEP_KEY0, "11111111111111111111111111", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX, NULL, 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	if (add_wifi)
		fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_NONE,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_ap_wpa_psk_connection_base (const char *key_mgmt,
                                 const char *auth_alg,
                                 guint32 flags,
                                 guint32 wpa_flags,
                                 guint32 rsn_flags,
                                 gboolean add_wifi,
                                 NMConnection *expected)
{
	NMConnection *src;
	const char *ssid = "blahblah";
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData exp_wifi[] = {
		{ NM_SETTING_WIRELESS_SSID, ssid, 0 },
		{ NM_SETTING_WIRELESS_MODE, "infrastructure", 0 },
		{ NM_SETTING_WIRELESS_SEC, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, 0 },
		{ NULL } };
	const KeyData both_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, key_mgmt, 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, auth_alg, 0 },
	    { NM_SETTING_WIRELESS_SECURITY_PSK, "asdfasdfasdfasdfasdfafs", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	if (add_wifi)
		fill_wifi_empty (src);
	fill_wsec (src, both_wsec);
	success = complete_connection (ssid, bssid, NM_802_11_MODE_INFRA,
	                               flags, wpa_flags, rsn_flags,
	                               FALSE, src, &error);
	if (expected) {
		fill_wifi (expected, exp_wifi);
		fill_wsec (expected, both_wsec);
	}
	COMPARE (src, expected, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

static void
test_open_ap_wpa_psk_connection_1 (void)
{
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_open_ap_wpa_psk_connection_2 (void)
{
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_open_ap_wpa_psk_connection_3 (void)
{
	test_ap_wpa_psk_connection_base (NULL, "open",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_open_ap_wpa_psk_connection_4 (void)
{
	test_ap_wpa_psk_connection_base (NULL, "shared",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_open_ap_wpa_psk_connection_5 (void)
{
	test_ap_wpa_psk_connection_base ("wpa-psk", "open",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

/*******************************************/

static void
test_ap_wpa_eap_connection_base (const char *key_mgmt,
                                 const char *auth_alg,
                                 guint32 flags,
                                 guint32 wpa_flags,
                                 guint32 rsn_flags,
                                 gboolean add_wifi,
                                 guint error_domain,
                                 guint error_code)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_empty[] = { { NULL } };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, key_mgmt, 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, auth_alg, 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	if (add_wifi)
		fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	fill_8021x (src, src_empty);
	success = complete_connection ("blahblah", bssid, NM_802_11_MODE_INFRA,
	                               flags, wpa_flags, rsn_flags,
	                               FALSE, src, &error);
	if (!wpa_flags && !rsn_flags) {
		if (!flags) {
			/* Failure expected */
			COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);
		} else if (flags & NM_802_11_AP_FLAGS_PRIVACY) {
			COMPARE (src, NULL, success, error, error_domain, error_code);
		}
	} else
		g_assert_not_reached ();

	g_object_unref (src);
}

static void
test_open_ap_wpa_eap_connection_1 (void)
{
	test_ap_wpa_eap_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, 0, 0);
}

static void
test_open_ap_wpa_eap_connection_2 (void)
{
	test_ap_wpa_eap_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 TRUE, 0, 0);
}

static void
test_open_ap_wpa_eap_connection_3 (void)
{
	test_ap_wpa_eap_connection_base (NULL, "open",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, 0, 0);
}

static void
test_open_ap_wpa_eap_connection_4 (void)
{
	test_ap_wpa_eap_connection_base (NULL, "shared",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, 0, 0);
}

static void
test_open_ap_wpa_eap_connection_5 (void)
{
	test_ap_wpa_eap_connection_base ("wpa-eap", "open",
	                                 NM_802_11_AP_FLAGS_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, 0, 0);
}

/*******************************************/

static void
test_priv_ap_empty_connection (void)
{
	NMConnection *src, *expected;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *ssid = "blahblah";
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);

	/* Static WEP connection expected */
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, TRUE);
	fill_wsec (expected, exp_wsec);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
	g_object_unref (expected);
}

/*******************************************/

static void
test_priv_ap_leap_connection_1 (gboolean add_wifi)
{
	NMConnection *src, *expected;
	const char *ssid = "blahblah";
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *leap_username = "Bill Smith";
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME, leap_username, 0 },
	    { NULL } };
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "leap", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME, leap_username, 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	if (add_wifi)
		fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect success here; since LEAP APs just set the 'privacy' flag
	 * there's no way to determine from the AP's beacon whether it's static WEP,
	 * dynamic WEP, or LEAP.
	 */
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, TRUE);
	fill_wsec (expected, exp_wsec);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
}

/*******************************************/

static void
test_priv_ap_leap_connection_2 (void)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "leap", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure here, we need a LEAP username */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_LEAP_REQUIRES_USERNAME);

	g_object_unref (src);
}

/*******************************************/

static void
test_priv_ap_dynamic_wep_1 (void)
{
	NMConnection *src, *expected;
	const char *ssid = "blahblah";
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };
	const KeyData both_8021x[] = {
	    { NM_SETTING_802_1X_EAP, "peap", 0 },
	    { NM_SETTING_802_1X_IDENTITY, "Bill Smith", 0 },
	    { NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", 0 },
	    { NULL } };
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_PAIRWISE, "wep40", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_PAIRWISE, "wep104", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_GROUP, "wep40", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_GROUP, "wep104", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	fill_8021x (src, both_8021x);
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);

	/* We expect a completed Dynamic WEP connection */
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, TRUE);
	fill_wsec (expected, exp_wsec);
	fill_8021x (expected, both_8021x);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
}

/*******************************************/

static void
test_priv_ap_dynamic_wep_2 (void)
{
	NMConnection *src, *expected;
	const char *ssid = "blahblah";
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };
	const KeyData both_8021x[] = {
	    { NM_SETTING_802_1X_EAP, "peap", 0 },
	    { NM_SETTING_802_1X_IDENTITY, "Bill Smith", 0 },
	    { NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", 0 },
	    { NULL } };
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_PAIRWISE, "wep40", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_PAIRWISE, "wep104", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_GROUP, "wep40", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_GROUP, "wep104", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	fill_8021x (src, both_8021x);
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);

	/* We expect a completed Dynamic WEP connection */
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, TRUE);
	fill_wsec (expected, exp_wsec);
	fill_8021x (expected, both_8021x);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
}

/*******************************************/

static void
test_priv_ap_dynamic_wep_3 (void)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "shared", 0 },
	    { NULL } };
	const KeyData src_8021x[] = {
	    { NM_SETTING_802_1X_EAP, "peap", 0 },
	    { NM_SETTING_802_1X_IDENTITY, "Bill Smith", 0 },
	    { NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	fill_8021x (src, src_8021x);
	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               NM_802_11_AP_SEC_NONE, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* Expect failure; shared is not compatible with dynamic WEP */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_priv_ap_wpa_psk_connection_1 (void)
{
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_priv_ap_wpa_psk_connection_2 (void)
{
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 TRUE, NULL);
}

static void
test_priv_ap_wpa_psk_connection_3 (void)
{
	test_ap_wpa_psk_connection_base (NULL, "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_priv_ap_wpa_psk_connection_4 (void)
{
	test_ap_wpa_psk_connection_base (NULL, "shared",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_priv_ap_wpa_psk_connection_5 (void)
{
	test_ap_wpa_psk_connection_base ("wpa-psk", "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

/*******************************************/

static void
test_priv_ap_wpa_eap_connection_1 (void)
{
	test_ap_wpa_eap_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE,
	                                 NM_SETTING_802_1X_ERROR,
	                                 NM_SETTING_802_1X_ERROR_MISSING_PROPERTY);
}

static void
test_priv_ap_wpa_eap_connection_2 (void)
{
	test_ap_wpa_eap_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 TRUE,
	                                 NM_SETTING_802_1X_ERROR,
	                                 NM_SETTING_802_1X_ERROR_MISSING_PROPERTY);
}

static void
test_priv_ap_wpa_eap_connection_3 (void)
{
	test_ap_wpa_eap_connection_base (NULL, "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE,
	                                 NM_SETTING_802_1X_ERROR,
	                                 NM_SETTING_802_1X_ERROR_MISSING_PROPERTY);
}

static void
test_priv_ap_wpa_eap_connection_4 (void)
{
	test_ap_wpa_eap_connection_base (NULL, "shared",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE,
	                                 NM_SETTING_WIRELESS_SECURITY_ERROR,
	                                 NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);
}

static void
test_priv_ap_wpa_eap_connection_5 (void)
{
	test_ap_wpa_eap_connection_base ("wpa-eap", "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE,
	                                 NM_SETTING_WIRELESS_SECURITY_ERROR,
	                                 NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);
}

/*******************************************/

#define WPA_PSK_CAPS (NM_802_11_AP_SEC_PAIR_TKIP | NM_802_11_AP_SEC_KEY_MGMT_PSK)

static void
test_wpa_ap_empty_connection (void)
{
	NMConnection *src, *expected;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *ssid = "blahblah";
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               WPA_PSK_CAPS, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);

	/* Static WEP connection expected */
	expected = create_basic (ssid, NULL, NM_802_11_MODE_INFRA, TRUE);
	fill_wsec (expected, exp_wsec);
	COMPARE (src, expected, success, error, 0, 0);

	g_object_unref (src);
	g_object_unref (expected);
}

/*******************************************/

static void
test_wpa_ap_leap_connection_1 (void)
{
	NMConnection *src;
	const char *ssid = "blahblah";
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const char *leap_username = "Bill Smith";
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME, leap_username, 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection (ssid, bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               WPA_PSK_CAPS, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* Expect failure here; WPA APs don't support old-school LEAP */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_wpa_ap_leap_connection_2 (void)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "leap", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               WPA_PSK_CAPS, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure here, we need a LEAP username */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_wpa_ap_dynamic_wep_connection (void)
{
	NMConnection *src;
	const guint8 bssid[ETH_ALEN] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
	const KeyData src_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", 0 },
	    { NULL } };
	gboolean success;
	GError *error = NULL;

	src = nm_connection_new ();
	fill_wifi_empty (src);
	fill_wsec (src, src_wsec);
	success = complete_connection ("blahblah", bssid,
	                               NM_802_11_MODE_INFRA, NM_802_11_AP_FLAGS_PRIVACY,
	                               WPA_PSK_CAPS, NM_802_11_AP_SEC_NONE,
	                               FALSE,
	                               src, &error);
	/* We expect failure here since Dynamic WEP is incompatible with WPA */
	COMPARE (src, NULL, success, error, NM_SETTING_WIRELESS_SECURITY_ERROR, NM_SETTING_WIRELESS_SECURITY_ERROR_INVALID_PROPERTY);

	g_object_unref (src);
}

/*******************************************/

static void
test_wpa_ap_wpa_psk_connection_1 (void)
{
	NMConnection *expected;
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };

	expected = nm_connection_new ();
	fill_wsec (expected, exp_wsec);
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 WPA_PSK_CAPS,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, expected);
	g_object_unref (expected);
}

static void
test_wpa_ap_wpa_psk_connection_2 (void)
{
	NMConnection *expected;
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };

	expected = nm_connection_new ();
	fill_wsec (expected, exp_wsec);
	test_ap_wpa_psk_connection_base (NULL, NULL,
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 TRUE, NULL);
	g_object_unref (expected);
}

static void
test_wpa_ap_wpa_psk_connection_3 (void)
{
	NMConnection *expected;
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };

	expected = nm_connection_new ();
	fill_wsec (expected, exp_wsec);
	test_ap_wpa_psk_connection_base (NULL, "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
	g_object_unref (expected);
}

static void
test_wpa_ap_wpa_psk_connection_4 (void)
{
	test_ap_wpa_psk_connection_base (NULL, "shared",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
}

static void
test_wpa_ap_wpa_psk_connection_5 (void)
{
	NMConnection *expected;
	const KeyData exp_wsec[] = {
	    { NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", 0 },
	    { NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open", 0 },
	    { NULL } };

	expected = nm_connection_new ();
	fill_wsec (expected, exp_wsec);
	test_ap_wpa_psk_connection_base ("wpa-psk", "open",
	                                 NM_802_11_AP_FLAGS_PRIVACY,
	                                 NM_802_11_AP_SEC_NONE,
	                                 NM_802_11_AP_SEC_NONE,
	                                 FALSE, NULL);
	g_object_unref (expected);
}

/*******************************************/

#if GLIB_CHECK_VERSION(2,25,12)
typedef GTestFixtureFunc TCFunc;
#else
typedef void (*TCFunc)(void);
#endif

#define TESTCASE(t, d) g_test_create_case (#t, 0, (gconstpointer) d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
	GTestSuite *suite;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

	suite = g_test_get_root ();

	g_test_suite_add (suite, TESTCASE (test_lock_bssid, NULL));

	/* Open AP tests; make sure that connections to be completed that have
	 * various security-related settings already set cause the completion
	 * to fail.
	 */
	g_test_suite_add (suite, TESTCASE (test_open_ap_empty_connection, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_leap_connection_1, TRUE));
	g_test_suite_add (suite, TESTCASE (test_open_ap_leap_connection_1, FALSE));
	g_test_suite_add (suite, TESTCASE (test_open_ap_leap_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wep_connection, TRUE));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wep_connection, FALSE));

	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_psk_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_psk_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_psk_connection_3, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_psk_connection_4, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_psk_connection_5, NULL));

	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_eap_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_eap_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_eap_connection_3, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_eap_connection_4, NULL));
	g_test_suite_add (suite, TESTCASE (test_open_ap_wpa_eap_connection_5, NULL));

	/* WEP AP tests */
	g_test_suite_add (suite, TESTCASE (test_priv_ap_empty_connection, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_leap_connection_1, FALSE));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_leap_connection_2, FALSE));

	g_test_suite_add (suite, TESTCASE (test_priv_ap_dynamic_wep_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_dynamic_wep_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_dynamic_wep_3, NULL));

	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_psk_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_psk_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_psk_connection_3, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_psk_connection_4, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_psk_connection_5, NULL));

	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_eap_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_eap_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_eap_connection_3, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_eap_connection_4, NULL));
	g_test_suite_add (suite, TESTCASE (test_priv_ap_wpa_eap_connection_5, NULL));

	/* WPA-PSK tests */
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_empty_connection, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_leap_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_leap_connection_2, NULL));

	g_test_suite_add (suite, TESTCASE (test_wpa_ap_dynamic_wep_connection, NULL));

	g_test_suite_add (suite, TESTCASE (test_wpa_ap_wpa_psk_connection_1, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_wpa_psk_connection_2, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_wpa_psk_connection_3, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_wpa_psk_connection_4, NULL));
	g_test_suite_add (suite, TESTCASE (test_wpa_ap_wpa_psk_connection_5, NULL));

	return g_test_run ();
}

