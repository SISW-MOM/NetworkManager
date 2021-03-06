// SPDX-License-Identifier: LGPL-2.1+
/*
 * Copyright (C) 2011 - 2013 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-setting-bond.h"

#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nm-utils.h"
#include "nm-utils-private.h"
#include "nm-connection-private.h"
#include "nm-setting-infiniband.h"
#include "nm-core-internal.h"

/*****************************************************************************/

/**
 * SECTION:nm-setting-bond
 * @short_description: Describes connection properties for bonds
 *
 * The #NMSettingBond object is a #NMSetting subclass that describes properties
 * necessary for bond connections.
 **/

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE (NMSettingBond,
	PROP_OPTIONS,
);

typedef struct {
	GHashTable *options;
	NMUtilsNamedValue *options_idx_cache;
} NMSettingBondPrivate;

G_DEFINE_TYPE (NMSettingBond, nm_setting_bond, NM_TYPE_SETTING)

#define NM_SETTING_BOND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_BOND, NMSettingBondPrivate))

/*****************************************************************************/

static const char *const valid_options_lst[] = {
	NM_SETTING_BOND_OPTION_MODE,
	NM_SETTING_BOND_OPTION_MIIMON,
	NM_SETTING_BOND_OPTION_DOWNDELAY,
	NM_SETTING_BOND_OPTION_UPDELAY,
	NM_SETTING_BOND_OPTION_ARP_INTERVAL,
	NM_SETTING_BOND_OPTION_ARP_IP_TARGET,
	NM_SETTING_BOND_OPTION_ARP_VALIDATE,
	NM_SETTING_BOND_OPTION_PRIMARY,
	NM_SETTING_BOND_OPTION_PRIMARY_RESELECT,
	NM_SETTING_BOND_OPTION_FAIL_OVER_MAC,
	NM_SETTING_BOND_OPTION_USE_CARRIER,
	NM_SETTING_BOND_OPTION_AD_SELECT,
	NM_SETTING_BOND_OPTION_XMIT_HASH_POLICY,
	NM_SETTING_BOND_OPTION_RESEND_IGMP,
	NM_SETTING_BOND_OPTION_LACP_RATE,
	NM_SETTING_BOND_OPTION_ACTIVE_SLAVE,
	NM_SETTING_BOND_OPTION_AD_ACTOR_SYS_PRIO,
	NM_SETTING_BOND_OPTION_AD_ACTOR_SYSTEM,
	NM_SETTING_BOND_OPTION_AD_USER_PORT_KEY,
	NM_SETTING_BOND_OPTION_ALL_SLAVES_ACTIVE,
	NM_SETTING_BOND_OPTION_ARP_ALL_TARGETS,
	NM_SETTING_BOND_OPTION_MIN_LINKS,
	NM_SETTING_BOND_OPTION_NUM_GRAT_ARP,
	NM_SETTING_BOND_OPTION_NUM_UNSOL_NA,
	NM_SETTING_BOND_OPTION_PACKETS_PER_SLAVE,
	NM_SETTING_BOND_OPTION_TLB_DYNAMIC_LB,
	NM_SETTING_BOND_OPTION_LP_INTERVAL,
	NULL,
};

typedef struct {
	const char *val;
	NMBondOptionType opt_type;
	guint min;
	guint max;
	const char *const*list;
} OptionMeta;

static gboolean
_nm_assert_bond_meta (const OptionMeta *option_meta)
{
	nm_assert (option_meta);

	switch (option_meta->opt_type) {
	case NM_BOND_OPTION_TYPE_BOTH:
		nm_assert (option_meta->val);
		nm_assert (option_meta->list);
		nm_assert (option_meta->list[0]);
		nm_assert (option_meta->min == 0);
		nm_assert (option_meta->max == NM_PTRARRAY_LEN (option_meta->list) - 1);
		nm_assert (g_strv_contains (option_meta->list, option_meta->val));
		return TRUE;
	case NM_BOND_OPTION_TYPE_INT:
		nm_assert (option_meta->val);
		nm_assert (!option_meta->list);
		nm_assert (option_meta->min < option_meta->max);
		nm_assert (NM_STRCHAR_ALL (option_meta->val, ch, g_ascii_isdigit (ch)));
		nm_assert (NM_STRCHAR_ALL (option_meta->val, ch, g_ascii_isdigit (ch)));
		nm_assert (({
		              _nm_utils_ascii_str_to_uint64 (option_meta->val, 10, option_meta->min, option_meta->max, 0);
		              errno == 0;
		            }));
		return TRUE;
	case NM_BOND_OPTION_TYPE_IP:
	case NM_BOND_OPTION_TYPE_IFNAME:
		nm_assert (option_meta->val);
		/* fall-through */
	case NM_BOND_OPTION_TYPE_MAC:
		nm_assert (!option_meta->list);
		nm_assert (option_meta->min == 0);
		nm_assert (option_meta->max == 0);
		return TRUE;
	}

	nm_assert_not_reached ();
	return FALSE;
}

static char const *const _option_default_strv_ad_select[]        = NM_MAKE_STRV ("stable", "bandwidth", "count");
static char const *const _option_default_strv_arp_all_targets[]  = NM_MAKE_STRV ("any", "all");
static char const *const _option_default_strv_arp_validate[]     = NM_MAKE_STRV ("none", "active", "backup", "all", "filter", "filter_active", "filter_backup");
static char const *const _option_default_strv_fail_over_mac[]    = NM_MAKE_STRV ("none", "active", "follow");
static char const *const _option_default_strv_lacp_rate[]        = NM_MAKE_STRV ("slow", "fast");
static char const *const _option_default_strv_mode[]             = NM_MAKE_STRV ("balance-rr", "active-backup", "balance-xor", "broadcast", "802.3ad", "balance-tlb", "balance-alb");
static char const *const _option_default_strv_primary_reselect[] = NM_MAKE_STRV ("always", "better", "failure");
static char const *const _option_default_strv_xmit_hash_policy[] = NM_MAKE_STRV ("layer2", "layer3+4", "layer2+3", "encap2+3", "encap3+4");

static
NM_UTILS_STRING_TABLE_LOOKUP_STRUCT_DEFINE (
	_get_option_meta,
	OptionMeta,
	{
		G_STATIC_ASSERT_EXPR (G_N_ELEMENTS (LIST) == G_N_ELEMENTS (valid_options_lst) - 1);

		if (NM_MORE_ASSERT_ONCE (5)) {
			int i;

			nm_assert (G_N_ELEMENTS (LIST) == NM_PTRARRAY_LEN (valid_options_lst));
			for (i = 0; i < G_N_ELEMENTS (LIST); i++)
				_nm_assert_bond_meta (&LIST[i].value);
		}
	},
	{ return NULL; },
	{ NM_SETTING_BOND_OPTION_ACTIVE_SLAVE,      { "",           NM_BOND_OPTION_TYPE_IFNAME                                                   } },
	{ NM_SETTING_BOND_OPTION_AD_ACTOR_SYS_PRIO, { "65535",      NM_BOND_OPTION_TYPE_INT,   1, 65535                                          } },
	{ NM_SETTING_BOND_OPTION_AD_ACTOR_SYSTEM,   { NULL,         NM_BOND_OPTION_TYPE_MAC                                                      } },
	{ NM_SETTING_BOND_OPTION_AD_SELECT,         { "stable",     NM_BOND_OPTION_TYPE_BOTH,  0, 2,       _option_default_strv_ad_select        } },
	{ NM_SETTING_BOND_OPTION_AD_USER_PORT_KEY,  { "0",          NM_BOND_OPTION_TYPE_INT,   0, 1023                                           } },
	{ NM_SETTING_BOND_OPTION_ALL_SLAVES_ACTIVE, { "0",          NM_BOND_OPTION_TYPE_INT,   0, 1                                              } },
	{ NM_SETTING_BOND_OPTION_ARP_ALL_TARGETS,   { "any",        NM_BOND_OPTION_TYPE_BOTH,  0, 1,       _option_default_strv_arp_all_targets  } },
	{ NM_SETTING_BOND_OPTION_ARP_INTERVAL,      { "0",          NM_BOND_OPTION_TYPE_INT,   0, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_ARP_IP_TARGET,     { "",           NM_BOND_OPTION_TYPE_IP                                                       } },
	{ NM_SETTING_BOND_OPTION_ARP_VALIDATE,      { "none",       NM_BOND_OPTION_TYPE_BOTH,  0, 6,       _option_default_strv_arp_validate     } },
	{ NM_SETTING_BOND_OPTION_DOWNDELAY,         { "0",          NM_BOND_OPTION_TYPE_INT,   0, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_FAIL_OVER_MAC,     { "none",       NM_BOND_OPTION_TYPE_BOTH,  0, 2,       _option_default_strv_fail_over_mac    } },
	{ NM_SETTING_BOND_OPTION_LACP_RATE,         { "slow",       NM_BOND_OPTION_TYPE_BOTH,  0, 1,       _option_default_strv_lacp_rate        } },
	{ NM_SETTING_BOND_OPTION_LP_INTERVAL,       { "1",          NM_BOND_OPTION_TYPE_INT,   1, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_MIIMON,            { "100",        NM_BOND_OPTION_TYPE_INT,   0, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_MIN_LINKS,         { "0",          NM_BOND_OPTION_TYPE_INT,   0, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_MODE,              { "balance-rr", NM_BOND_OPTION_TYPE_BOTH,  0, 6,       _option_default_strv_mode             } },
	{ NM_SETTING_BOND_OPTION_NUM_GRAT_ARP,      { "1",          NM_BOND_OPTION_TYPE_INT,   0, 255                                            } },
	{ NM_SETTING_BOND_OPTION_NUM_UNSOL_NA,      { "1",          NM_BOND_OPTION_TYPE_INT,   0, 255                                            } },
	{ NM_SETTING_BOND_OPTION_PACKETS_PER_SLAVE, { "1",          NM_BOND_OPTION_TYPE_INT,   0, 65535                                          } },
	{ NM_SETTING_BOND_OPTION_PRIMARY,           { "",           NM_BOND_OPTION_TYPE_IFNAME                                                   } },
	{ NM_SETTING_BOND_OPTION_PRIMARY_RESELECT,  { "always",     NM_BOND_OPTION_TYPE_BOTH,  0, 2,       _option_default_strv_primary_reselect } },
	{ NM_SETTING_BOND_OPTION_RESEND_IGMP,       { "1",          NM_BOND_OPTION_TYPE_INT,   0, 255                                            } },
	{ NM_SETTING_BOND_OPTION_TLB_DYNAMIC_LB,    { "1",          NM_BOND_OPTION_TYPE_INT,   0, 1                                              } },
	{ NM_SETTING_BOND_OPTION_UPDELAY,           { "0",          NM_BOND_OPTION_TYPE_INT,   0, G_MAXINT                                       } },
	{ NM_SETTING_BOND_OPTION_USE_CARRIER,       { "1",          NM_BOND_OPTION_TYPE_INT,   0, 1                                              } },
	{ NM_SETTING_BOND_OPTION_XMIT_HASH_POLICY,  { "layer2",     NM_BOND_OPTION_TYPE_BOTH,  0, 4,       _option_default_strv_xmit_hash_policy } },
);

/*****************************************************************************/

static int
_atoi (const char *value)
{
	int v;

	v = _nm_utils_ascii_str_to_int64 (value, 10, 0, G_MAXINT, -1);
	nm_assert (v >= 0);
	return v;
};

/**
 * nm_setting_bond_get_num_options:
 * @setting: the #NMSettingBond
 *
 * Returns the number of options that should be set for this bond when it
 * is activated. This can be used to retrieve each option individually
 * using nm_setting_bond_get_option().
 *
 * Returns: the number of bonding options
 **/
guint32
nm_setting_bond_get_num_options (NMSettingBond *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), 0);

	return g_hash_table_size (NM_SETTING_BOND_GET_PRIVATE (setting)->options);
}

static int
_get_option_sort (gconstpointer p_a, gconstpointer p_b, gpointer _unused)
{
	const char *a = *((const char *const*) p_a);
	const char *b = *((const char *const*) p_b);

	NM_CMP_DIRECT (nm_streq (b, NM_SETTING_BOND_OPTION_MODE),
	               nm_streq (a, NM_SETTING_BOND_OPTION_MODE));
	NM_CMP_DIRECT_STRCMP (a, b);
	nm_assert_not_reached ();
	return 0;
}

static void
_ensure_options_idx_cache (NMSettingBondPrivate *priv)
{
	if (!G_UNLIKELY (priv->options_idx_cache))
		priv->options_idx_cache = nm_utils_named_values_from_str_dict_with_sort (priv->options, NULL, _get_option_sort, NULL);
}

/**
 * nm_setting_bond_get_option:
 * @setting: the #NMSettingBond
 * @idx: index of the desired option, from 0 to
 * nm_setting_bond_get_num_options() - 1
 * @out_name: (out) (transfer none): on return, the name of the bonding option;
 *   this value is owned by the setting and should not be modified
 * @out_value: (out) (transfer none): on return, the value of the name of the
 *   bonding option; this value is owned by the setting and should not be
 *   modified
 *
 * Given an index, return the value of the bonding option at that index.  Indexes
 * are *not* guaranteed to be static across modifications to options done by
 * nm_setting_bond_add_option() and nm_setting_bond_remove_option(),
 * and should not be used to refer to options except for short periods of time
 * such as during option iteration.
 *
 * Returns: %TRUE on success if the index was valid and an option was found,
 * %FALSE if the index was invalid (ie, greater than the number of options
 * currently held by the setting)
 **/
gboolean
nm_setting_bond_get_option (NMSettingBond *setting,
                            guint32 idx,
                            const char **out_name,
                            const char **out_value)
{
	NMSettingBondPrivate *priv;
	guint len;

	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), FALSE);

	priv = NM_SETTING_BOND_GET_PRIVATE (setting);

	len = g_hash_table_size (priv->options);
	if (idx >= len)
		return FALSE;

	_ensure_options_idx_cache (priv);

	NM_SET_OUT (out_name, priv->options_idx_cache[idx].name);
	NM_SET_OUT (out_value, priv->options_idx_cache[idx].value_str);
	return TRUE;
}

static gboolean
validate_int (const char *name, const char *value, const OptionMeta *option_meta)
{
	guint64 num;

	if (!NM_STRCHAR_ALL (value, ch, g_ascii_isdigit (ch)))
		return FALSE;

	num = _nm_utils_ascii_str_to_uint64 (value, 10, option_meta->min, option_meta->max, G_MAXUINT64);
	if (   num == G_MAXUINT64
	    && errno != 0)
		return FALSE;

	return TRUE;
}

static gboolean
validate_list (const char *name, const char *value, const OptionMeta *option_meta)
{
	int i;

	nm_assert (option_meta->list);

	for (i = 0; option_meta->list[i]; i++) {
		if (nm_streq (option_meta->list[i], value))
			return TRUE;
	}
	return FALSE;
}

static gboolean
validate_ip (const char *name, const char *value)
{
	gs_free char *value_clone = NULL;
	struct in_addr addr;

	if (!value || !value[0])
		return FALSE;

	value_clone = g_strdup (value);
	value = value_clone;
	for (;;) {
		char *eow;

		/* we do not skip over empty words. E.g
		 * "192.168.1.1," is an error.
		 *
		 * ... for no particular reason. */

		eow = strchr (value, ',');
		if (eow)
			*eow = '\0';

		if (inet_pton (AF_INET, value, &addr) != 1)
			return FALSE;

		if (!eow)
			break;
		value = eow + 1;
	}
	return TRUE;
}

static gboolean
validate_ifname (const char *name, const char *value)
{
	return nm_utils_ifname_valid_kernel (value, NULL);
}

/**
 * nm_setting_bond_validate_option:
 * @name: the name of the option to validate
 * @value: the value of the option to validate
 *
 * Checks whether @name is a valid bond option and @value is a valid value for
 * the @name. If @value is %NULL, the function only validates the option name.
 *
 * Returns: %TRUE, if the @value is valid for the given name.
 * If the @name is not a valid option, %FALSE will be returned.
 **/
gboolean
nm_setting_bond_validate_option (const char *name,
                                 const char *value)
{
	const OptionMeta *option_meta;

	option_meta = _get_option_meta (name);
	if (!option_meta)
		return FALSE;

	if (!value)
		return TRUE;

	switch (option_meta->opt_type) {
	case NM_BOND_OPTION_TYPE_INT:
		return validate_int (name, value, option_meta);
	case NM_BOND_OPTION_TYPE_BOTH:
		return (   validate_int (name, value, option_meta)
		        || validate_list (name, value, option_meta));
	case NM_BOND_OPTION_TYPE_IP:
		return validate_ip (name, value);
	case NM_BOND_OPTION_TYPE_MAC:
		return nm_utils_hwaddr_valid (value, ETH_ALEN);
	case NM_BOND_OPTION_TYPE_IFNAME:
		return validate_ifname (name, value);
	}

	nm_assert_not_reached ();
	return FALSE;
}

/**
 * nm_setting_bond_get_option_by_name:
 * @setting: the #NMSettingBond
 * @name: the option name for which to retrieve the value
 *
 * Returns the value associated with the bonding option specified by
 * @name, if it exists.
 *
 * Returns: the value, or %NULL if the key/value pair was never added to the
 * setting; the value is owned by the setting and must not be modified
 **/
const char *
nm_setting_bond_get_option_by_name (NMSettingBond *setting,
                                    const char *name)
{
	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), NULL);

	if (!nm_setting_bond_validate_option (name, NULL))
		return NULL;

	return g_hash_table_lookup (NM_SETTING_BOND_GET_PRIVATE (setting)->options, name);
}

/**
 * nm_setting_bond_add_option:
 * @setting: the #NMSettingBond
 * @name: name for the option
 * @value: value for the option
 *
 * Add an option to the table.  The option is compared to an internal list
 * of allowed options.  Option names may contain only alphanumeric characters
 * (ie [a-zA-Z0-9]).  Adding a new name replaces any existing name/value pair
 * that may already exist.
 *
 * The order of how to set several options is relevant because there are options
 * that conflict with each other.
 *
 * Returns: %TRUE if the option was valid and was added to the internal option
 * list, %FALSE if it was not.
 **/
gboolean
nm_setting_bond_add_option (NMSettingBond *setting,
                            const char *name,
                            const char *value)
{
	NMSettingBondPrivate *priv;

	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), FALSE);

	if (!value || !nm_setting_bond_validate_option (name, value))
		return FALSE;

	priv = NM_SETTING_BOND_GET_PRIVATE (setting);

	nm_clear_g_free (&priv->options_idx_cache);
	g_hash_table_insert (priv->options, g_strdup (name), g_strdup (value));

	if (nm_streq (name, NM_SETTING_BOND_OPTION_MIIMON)) {
		if (!nm_streq (value, "0")) {
			g_hash_table_remove (priv->options, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
			g_hash_table_remove (priv->options, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
		}
	} else if (nm_streq (name, NM_SETTING_BOND_OPTION_ARP_INTERVAL)) {
		if (!nm_streq  (value, "0")) {
			g_hash_table_remove (priv->options, NM_SETTING_BOND_OPTION_MIIMON);
			g_hash_table_remove (priv->options, NM_SETTING_BOND_OPTION_DOWNDELAY);
			g_hash_table_remove (priv->options, NM_SETTING_BOND_OPTION_UPDELAY);
		}
	}

	_notify (setting, PROP_OPTIONS);

	return TRUE;
}

/**
 * nm_setting_bond_remove_option:
 * @setting: the #NMSettingBond
 * @name: name of the option to remove
 *
 * Remove the bonding option referenced by @name from the internal option
 * list.
 *
 * Returns: %TRUE if the option was found and removed from the internal option
 * list, %FALSE if it was not.
 **/
gboolean
nm_setting_bond_remove_option (NMSettingBond *setting,
                               const char *name)
{
	NMSettingBondPrivate *priv;
	gboolean found;

	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), FALSE);

	if (!nm_setting_bond_validate_option (name, NULL))
		return FALSE;

	priv = NM_SETTING_BOND_GET_PRIVATE (setting);

	nm_clear_g_free (&priv->options_idx_cache);
	found = g_hash_table_remove (priv->options, name);
	if (found)
		_notify (setting, PROP_OPTIONS);
	return found;
}

/**
 * nm_setting_bond_get_valid_options:
 * @setting: (allow-none): the #NMSettingBond
 *
 * Returns a list of valid bond options.
 *
 * The @setting argument is unused and may be passed as %NULL.
 *
 * Returns: (transfer none): a %NULL-terminated array of strings of valid bond options.
 **/
const char **
nm_setting_bond_get_valid_options  (NMSettingBond *setting)
{
	return (const char **) valid_options_lst;
}

/**
 * nm_setting_bond_get_option_default:
 * @setting: the #NMSettingBond
 * @name: the name of the option
 *
 * Returns: the value of the bond option if not overridden by an entry in
 *   the #NMSettingBond:options property.
 **/
const char *
nm_setting_bond_get_option_default (NMSettingBond *setting, const char *name)
{
	const OptionMeta *option_meta;
	const char *mode;

	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), NULL);

	option_meta = _get_option_meta (name);

	g_return_val_if_fail (option_meta, NULL);

	if (nm_streq (name, NM_SETTING_BOND_OPTION_AD_ACTOR_SYSTEM)) {
		/* The default value depends on the current mode */
		mode = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_MODE);
		if (NM_IN_STRSET (mode, "4", "802.3ad"))
			return "00:00:00:00:00:00";
		else
			return "";
	}

	return option_meta->val;
}

/**
 * nm_setting_bond_get_option_type:
 * @setting: the #NMSettingBond
 * @name: the name of the option
 *
 * Returns: the type of the bond option.
 **/
NMBondOptionType
_nm_setting_bond_get_option_type (NMSettingBond *setting, const char *name)
{
	const OptionMeta *option_meta;

	g_return_val_if_fail (NM_IS_SETTING_BOND (setting), NM_BOND_OPTION_TYPE_INT);

	option_meta = _get_option_meta (name);

	g_return_val_if_fail (option_meta, NM_BOND_OPTION_TYPE_INT);

	return option_meta->opt_type;
}

NM_UTILS_STRING_TABLE_LOOKUP_DEFINE (
	_nm_setting_bond_mode_from_string,
	NMBondMode,
	{ g_return_val_if_fail (name, NM_BOND_MODE_UNKNOWN); },
	{ return NM_BOND_MODE_UNKNOWN; },
	{ "802.3ad",       NM_BOND_MODE_8023AD       },
	{ "active-backup", NM_BOND_MODE_ACTIVEBACKUP },
	{ "balance-alb",   NM_BOND_MODE_ALB          },
	{ "balance-rr",    NM_BOND_MODE_ROUNDROBIN   },
	{ "balance-tlb",   NM_BOND_MODE_TLB          },
	{ "balance-xor",   NM_BOND_MODE_XOR          },
	{ "broadcast",     NM_BOND_MODE_BROADCAST    },
);

/*****************************************************************************/

#define BIT(x) (((guint32) 1) << (x))

static
NM_UTILS_STRING_TABLE_LOOKUP_DEFINE (
	_bond_option_unsupp_mode,
	guint32,
	{ ; },
	{ return 0; },
	{ NM_SETTING_BOND_OPTION_ACTIVE_SLAVE,      ~(BIT (NM_BOND_MODE_ACTIVEBACKUP) | BIT (NM_BOND_MODE_TLB) | BIT (NM_BOND_MODE_ALB)) },
	{ NM_SETTING_BOND_OPTION_AD_ACTOR_SYS_PRIO, ~(BIT (NM_BOND_MODE_8023AD)) },
	{ NM_SETTING_BOND_OPTION_AD_ACTOR_SYSTEM,   ~(BIT (NM_BOND_MODE_8023AD)) },
	{ NM_SETTING_BOND_OPTION_AD_USER_PORT_KEY,  ~(BIT (NM_BOND_MODE_8023AD)) },
	{ NM_SETTING_BOND_OPTION_ARP_INTERVAL,       (BIT (NM_BOND_MODE_8023AD)       | BIT (NM_BOND_MODE_TLB) | BIT (NM_BOND_MODE_ALB)) },
	{ NM_SETTING_BOND_OPTION_ARP_IP_TARGET,      (BIT (NM_BOND_MODE_8023AD)       | BIT (NM_BOND_MODE_TLB) | BIT (NM_BOND_MODE_ALB)) },
	{ NM_SETTING_BOND_OPTION_ARP_VALIDATE,       (BIT (NM_BOND_MODE_8023AD)       | BIT (NM_BOND_MODE_TLB) | BIT (NM_BOND_MODE_ALB)) },
	{ NM_SETTING_BOND_OPTION_LACP_RATE,         ~(BIT (NM_BOND_MODE_8023AD)) },
	{ NM_SETTING_BOND_OPTION_PACKETS_PER_SLAVE, ~(BIT (NM_BOND_MODE_ROUNDROBIN)) },
	{ NM_SETTING_BOND_OPTION_PRIMARY,           ~(BIT (NM_BOND_MODE_ACTIVEBACKUP) | BIT (NM_BOND_MODE_TLB) | BIT (NM_BOND_MODE_ALB)) },
	{ NM_SETTING_BOND_OPTION_TLB_DYNAMIC_LB,    ~(BIT (NM_BOND_MODE_TLB)) },
)

gboolean
_nm_setting_bond_option_supported (const char *option, NMBondMode mode)
{
	nm_assert (option);
	nm_assert (_NM_INT_NOT_NEGATIVE (mode) && mode < 32);

	return !NM_FLAGS_ANY (_bond_option_unsupp_mode (option), BIT (mode));
}

static gboolean
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingBondPrivate *priv = NM_SETTING_BOND_GET_PRIVATE (setting);
	int mode;
	int miimon = 0;
	int arp_interval = 0;
	int num_grat_arp = -1;
	int num_unsol_na = -1;
	const char *mode_orig;
	const char *mode_new;
	const char *arp_ip_target = NULL;
	const char *lacp_rate;
	const char *primary;
	NMBondMode bond_mode;
	guint i;
	const NMUtilsNamedValue *n;
	const char *value;

	_ensure_options_idx_cache (priv);

	if (priv->options_idx_cache) {
		for (i = 0; priv->options_idx_cache[i].name; i++) {
			n = &priv->options_idx_cache[i];

			if (   !n->value_str
			    || !nm_setting_bond_validate_option (n->name, n->value_str)) {
				g_set_error (error,
				             NM_CONNECTION_ERROR,
				             NM_CONNECTION_ERROR_INVALID_PROPERTY,
				             _("invalid option '%s' or its value '%s'"),
				             n->name, n->value_str);
				g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
				return FALSE;
			}
		}
	}

	value = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_MIIMON);
	if (value)
		miimon = atoi (value);
	value = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
	if (value)
		arp_interval = atoi (value);
	value = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_NUM_GRAT_ARP);
	if (value)
		num_grat_arp = atoi (value);
	value = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_NUM_UNSOL_NA);
	if (value)
		num_unsol_na = atoi (value);

	/* Can only set one of miimon and arp_interval */
	if (miimon > 0 && arp_interval > 0) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("only one of '%s' and '%s' can be set"),
		             NM_SETTING_BOND_OPTION_MIIMON,
		             NM_SETTING_BOND_OPTION_ARP_INTERVAL);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return FALSE;
	}

	/* Verify bond mode */
	mode_orig = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_MODE);
	if (!mode_orig) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("mandatory option '%s' is missing"),
		             NM_SETTING_BOND_OPTION_MODE);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return FALSE;
	}
	mode = nm_utils_bond_mode_string_to_int (mode_orig);
	if (mode == -1) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not a valid value for '%s'"),
		             value, NM_SETTING_BOND_OPTION_MODE);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return FALSE;
	}
	mode_new = nm_utils_bond_mode_int_to_string (mode);

	/* Make sure mode is compatible with other settings */
	if (NM_IN_STRSET (mode_new, "balance-alb",
	                            "balance-tlb")) {
		if (arp_interval > 0) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s=%s' is incompatible with '%s > 0'"),
			             NM_SETTING_BOND_OPTION_MODE, mode_new, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}
	}

	primary = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_PRIMARY);
	if (NM_IN_STRSET (mode_new, "active-backup")) {
		GError *tmp_error = NULL;

		if (primary && !nm_utils_ifname_valid_kernel (primary, &tmp_error)) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' is not valid for the '%s' option: %s"),
			             primary, NM_SETTING_BOND_OPTION_PRIMARY, tmp_error->message);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			g_error_free (tmp_error);
			return FALSE;
		}
	} else {
		if (primary) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option is only valid for '%s=%s'"),
			             NM_SETTING_BOND_OPTION_PRIMARY,
			             NM_SETTING_BOND_OPTION_MODE, "active-backup");
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}
	}

	if (   connection
	    && nm_connection_get_setting_infiniband (connection)) {
		if (!NM_IN_STRSET (mode_new, "active-backup")) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s=%s' is not a valid configuration for '%s'"),
			             NM_SETTING_BOND_OPTION_MODE, mode_new, NM_SETTING_INFINIBAND_SETTING_NAME);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}
	}

	if (miimon == 0) {
		gpointer delayopt;

		/* updelay and downdelay need miimon to be enabled to be valid */
		delayopt = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_UPDELAY);
		if (delayopt && _atoi (delayopt) > 0) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option requires '%s' option to be enabled"),
			             NM_SETTING_BOND_OPTION_UPDELAY, NM_SETTING_BOND_OPTION_MIIMON);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}

		delayopt = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_DOWNDELAY);
		if (delayopt && _atoi (delayopt) > 0) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option requires '%s' option to be enabled"),
			             NM_SETTING_BOND_OPTION_DOWNDELAY, NM_SETTING_BOND_OPTION_MIIMON);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}
	}

	/* arp_ip_target can only be used with arp_interval, and must
	 * contain a comma-separated list of IPv4 addresses.
	 */
	arp_ip_target = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
	if (arp_interval > 0) {
		char **addrs;
		guint32 addr;

		if (!arp_ip_target) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option requires '%s' option to be set"),
			             NM_SETTING_BOND_OPTION_ARP_INTERVAL, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}

		addrs = g_strsplit (arp_ip_target, ",", -1);
		if (!addrs[0]) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option is empty"),
			             NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			g_strfreev (addrs);
			return FALSE;
		}

		for (i = 0; addrs[i]; i++) {
			if (!inet_pton (AF_INET, addrs[i], &addr)) {
				g_set_error (error,
				             NM_CONNECTION_ERROR,
				             NM_CONNECTION_ERROR_INVALID_PROPERTY,
				             _("'%s' is not a valid IPv4 address for '%s' option"),
				             NM_SETTING_BOND_OPTION_ARP_IP_TARGET, addrs[i]);
				g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
				g_strfreev (addrs);
				return FALSE;
			}
		}
		g_strfreev (addrs);
	} else {
		if (arp_ip_target) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option requires '%s' option to be set"),
			             NM_SETTING_BOND_OPTION_ARP_IP_TARGET, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return FALSE;
		}
	}

	lacp_rate = g_hash_table_lookup (priv->options, NM_SETTING_BOND_OPTION_LACP_RATE);
	if (   lacp_rate
	    && !nm_streq0 (mode_new, "802.3ad")
	    && !NM_IN_STRSET (lacp_rate, "0", "slow")) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' option is only valid with mode '%s'"),
		             NM_SETTING_BOND_OPTION_LACP_RATE, "802.3ad");
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return FALSE;
	}

	if (   (num_grat_arp != -1 && num_unsol_na != -1)
	    && (num_grat_arp != num_unsol_na)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' and '%s' cannot have different values"),
		             NM_SETTING_BOND_OPTION_NUM_GRAT_ARP,
		             NM_SETTING_BOND_OPTION_NUM_UNSOL_NA);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return FALSE;
	}

	if (!_nm_connection_verify_required_interface_name (connection, error))
		return FALSE;

	/* *** errors above here should be always fatal, below NORMALIZABLE_ERROR *** */

	if (!nm_streq0 (mode_orig, mode_new)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' option should be string"),
		             NM_SETTING_BOND_OPTION_MODE);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
		return NM_SETTING_VERIFY_NORMALIZABLE;
	}

	/* normalize unsupported options for the current mode */
	bond_mode = _nm_setting_bond_mode_from_string (mode_new);
	for (i = 0; priv->options_idx_cache[i].name; i++) {
		n = &priv->options_idx_cache[i];
		if (!_nm_setting_bond_option_supported (n->name, bond_mode)) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' option is not valid with mode '%s'"),
			             n->name, mode_new);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_BOND_SETTING_NAME, NM_SETTING_BOND_OPTIONS);
			return NM_SETTING_VERIFY_NORMALIZABLE;
		}
	}

	return TRUE;
}

/*****************************************************************************/

static gboolean
options_equal_asym (NMSettingBond *s_bond,
                    NMSettingBond *s_bond2,
                    NMSettingCompareFlags flags)
{
	GHashTable *options2 = NM_SETTING_BOND_GET_PRIVATE (s_bond2)->options;
	GHashTableIter iter;
	const char *key, *value, *value2;

	g_hash_table_iter_init (&iter, NM_SETTING_BOND_GET_PRIVATE (s_bond)->options);
	while (g_hash_table_iter_next (&iter, (gpointer *) &key, (gpointer *) &value)) {

		if (NM_FLAGS_HAS (flags, NM_SETTING_COMPARE_FLAG_INFERRABLE)) {
			/* when doing an inferrable match, the active-slave should be ignored
			 * as it might be differ from the setting in the connection.
			 *
			 * Also, the fail_over_mac setting can change, see for example
			 * https://bugzilla.redhat.com/show_bug.cgi?id=1375558#c8 */
			if (NM_IN_STRSET (key, "fail_over_mac", "active_slave"))
				continue;
		}

		value2 = g_hash_table_lookup (options2, key);

		if (!value2) {
			if (nm_streq (key, "num_grat_arp"))
				value2 = g_hash_table_lookup (options2, "num_unsol_na");
			else if (nm_streq (key, "num_unsol_na"))
				value2 = g_hash_table_lookup (options2, "num_grat_arp");
		}

		if (!value2)
			value2 = nm_setting_bond_get_option_default (s_bond2, key);
		if (!nm_streq (value, value2))
			return FALSE;
	}

	return TRUE;
}

static gboolean
options_equal (NMSettingBond *s_bond,
               NMSettingBond *s_bond2,
               NMSettingCompareFlags flags)
{
	return    options_equal_asym (s_bond, s_bond2, flags)
	       && options_equal_asym (s_bond2, s_bond, flags);
}

static NMTernary
compare_property (const NMSettInfoSetting *sett_info,
                  guint property_idx,
                  NMConnection *con_a,
                  NMSetting *set_a,
                  NMConnection *con_b,
                  NMSetting *set_b,
                  NMSettingCompareFlags flags)
{
	if (nm_streq (sett_info->property_infos[property_idx].name, NM_SETTING_BOND_OPTIONS)) {
		return (   !set_b
		        || options_equal (NM_SETTING_BOND (set_a),
		                          NM_SETTING_BOND (set_b),
		                          flags));
	}

	return NM_SETTING_CLASS (nm_setting_bond_parent_class)->compare_property (sett_info,
	                                                                          property_idx,
	                                                                          con_a,
	                                                                          set_a,
	                                                                          con_b,
	                                                                          set_b,
	                                                                          flags);
}

/*****************************************************************************/

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSettingBondPrivate *priv = NM_SETTING_BOND_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_OPTIONS:
		g_value_take_boxed (value, _nm_utils_copy_strdict (priv->options));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingBondPrivate *priv = NM_SETTING_BOND_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_OPTIONS:
		nm_clear_g_free (&priv->options_idx_cache);
		g_hash_table_unref (priv->options);
		priv->options = _nm_utils_copy_strdict (g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*****************************************************************************/

static void
nm_setting_bond_init (NMSettingBond *setting)
{
	NMSettingBondPrivate *priv = NM_SETTING_BOND_GET_PRIVATE (setting);

	priv->options = g_hash_table_new_full (nm_str_hash, g_str_equal, g_free, g_free);

	/* Default values: */
	nm_setting_bond_add_option (setting, NM_SETTING_BOND_OPTION_MODE, "balance-rr");
}

/**
 * nm_setting_bond_new:
 *
 * Creates a new #NMSettingBond object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingBond object
 **/
NMSetting *
nm_setting_bond_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_BOND, NULL);
}

static void
finalize (GObject *object)
{
	NMSettingBondPrivate *priv = NM_SETTING_BOND_GET_PRIVATE (object);

	nm_clear_g_free (&priv->options_idx_cache);
	g_hash_table_destroy (priv->options);

	G_OBJECT_CLASS (nm_setting_bond_parent_class)->finalize (object);
}

static void
nm_setting_bond_class_init (NMSettingBondClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMSettingClass *setting_class = NM_SETTING_CLASS (klass);
	GArray *properties_override = _nm_sett_info_property_override_create_array ();

	g_type_class_add_private (klass, sizeof (NMSettingBondPrivate));

	object_class->get_property     = get_property;
	object_class->set_property     = set_property;
	object_class->finalize         = finalize;

	setting_class->verify           = verify;
	setting_class->compare_property = compare_property;

	/**
	 * NMSettingBond:options: (type GHashTable(utf8,utf8)):
	 *
	 * Dictionary of key/value pairs of bonding options.  Both keys and values
	 * must be strings. Option names must contain only alphanumeric characters
	 * (ie, [a-zA-Z0-9]).
	 **/
	/* ---ifcfg-rh---
	 * property: options
	 * variable: BONDING_OPTS
	 * description: Bonding options.
	 * example: BONDING_OPTS="miimon=100 mode=broadcast"
	 * ---end---
	 */
	obj_properties[PROP_OPTIONS] =
	    g_param_spec_boxed (NM_SETTING_BOND_OPTIONS, "", "",
	                        G_TYPE_HASH_TABLE,
	                        G_PARAM_READWRITE |
	                        NM_SETTING_PARAM_INFERRABLE |
	                        G_PARAM_STATIC_STRINGS);
	_nm_properties_override_gobj (properties_override, obj_properties[PROP_OPTIONS], &nm_sett_info_propert_type_strdict);

	 /* ---dbus---
	  * property: interface-name
	  * format: string
	  * description: Deprecated in favor of connection.interface-name, but can
	  *   be used for backward-compatibility with older daemons, to set the
	  *   bond's interface name.
	  * ---end---
	  */
	_nm_properties_override_dbus (properties_override, "interface-name", &nm_sett_info_propert_type_deprecated_interface_name);

	g_object_class_install_properties (object_class, _PROPERTY_ENUMS_LAST, obj_properties);

	_nm_setting_class_commit_full (setting_class, NM_META_SETTING_TYPE_BOND,
	                               NULL, properties_override);
}
