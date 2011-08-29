/*  mge-hid.c - data to monitor MGE UPS SYSTEMS HID (USB and serial) devices
 *
 *  Copyright (C) 2003 - 2009
 *  			Arnaud Quette <arnaud.quette@free.fr>
 *
 *  Sponsored by MGE UPS SYSTEMS <http://www.mgeups.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "main.h"		/* for getval() */
#include "usbhid-ups.h"
#include "mge-hid.h"

#define MGE_HID_VERSION		"MGE HID 1.22"

/* (prev. MGE Office Protection Systems, prev. MGE UPS SYSTEMS) */
/* Eaton */
#define MGE_VENDORID		0x0463

/* Dell */
#define DELL_VENDORID		0x047c

/* Powerware */
#define POWERWARE_VENDORID	0x0592

#ifndef SHUT_MODE
#include "usb-common.h"

/* USB IDs device table */
static usb_device_id_t mge_usb_device_table[] = {
	/* various models */
	{ USB_DEVICE(MGE_VENDORID, 0x0001), NULL },
	{ USB_DEVICE(MGE_VENDORID, 0xffff), NULL },

	/* various models */
	{ USB_DEVICE(DELL_VENDORID, 0xffff), NULL },

	/* PW 9140 */
	{ USB_DEVICE(POWERWARE_VENDORID, 0x0004), NULL },

	/* Terminating entry */
	{ -1, -1, NULL }
};
#endif

typedef enum {
	MGE_DEFAULT = 0,
	MGE_EVOLUTION = 0x100,		/* MGE Evolution series */
		MGE_EVOLUTION_650,
		MGE_EVOLUTION_850,
		MGE_EVOLUTION_1150,
		MGE_EVOLUTION_S_1250,
		MGE_EVOLUTION_1550,
		MGE_EVOLUTION_S_1750,
		MGE_EVOLUTION_2000,
		MGE_EVOLUTION_S_2500,
		MGE_EVOLUTION_S_3000,
	MGE_PULSAR_M = 0x200,		/* MGE Pulsar M series */
		MGE_PULSAR_M_2200,
		MGE_PULSAR_M_3000,
		MGE_PULSAR_M_3000_XL,
	MGE_PEGASUS = 0x400,
	MGE_3S
} models_type_t;

static models_type_t	mge_type = MGE_DEFAULT;
static char		mge_scratch_buf[20];

/* The HID path 'UPS.PowerSummary.Time' reports Unix time (ie the number of
 * seconds since 1970-01-01 00:00:00. This has to be split between ups.date and
 * ups.time */
static const char *mge_date_conversion_fun(double value)
{
	time_t	sec = value;

	if (strftime(mge_scratch_buf, sizeof(mge_scratch_buf), "%Y/%m/%d", localtime(&sec)) == 10) {
		return mge_scratch_buf;
	}

	upsdebugx(3, "%s: can't compute date %g", __func__, value);
	return NULL;
}

static const char *mge_time_conversion_fun(double value)
{
	time_t sec = value;

	if (strftime(mge_scratch_buf, sizeof(mge_scratch_buf), "%H:%M:%S", localtime(&sec)) == 8) {
		return mge_scratch_buf;
	}

	upsdebugx(3, "%s: can't compute time %g", __func__, value);
	return NULL;
}

#ifdef HAVE_STRPTIME
/* Conversion back retrieve ups.time to build the full unix time */
static double mge_date_conversion_nuf(const char *value)
{
	struct tm	mge_tm;

	/* build a full value (date) + time string */
	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%s %s", value, dstate_getinfo("ups.time"));

	if (strptime(mge_scratch_buf, "%Y/%m/%d %H:%M:%S", &mge_tm) != NULL) {
		return mktime(&mge_tm);
	}

	upsdebugx(3, "%s: can't compute date %s", __func__, value);
	return 0;
}

/* Conversion back retrieve ups.date to build the full unix time */
static double mge_time_conversion_nuf(const char *value)
{
	struct tm	mge_tm;

	/* build a full date + value (time) string */
	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%s %s", dstate_getinfo("ups.date"), value);

	if (strptime(mge_scratch_buf, "%Y/%m/%d %H:%M:%S", &mge_tm) != NULL) {
		return mktime(&mge_tm);
	}

	upsdebugx(3, "%s: can't compute time %s", __func__, value);
	return 0;
}

static info_lkp_t mge_date_conversion[] = {
	{ 0, NULL, mge_date_conversion_fun, mge_date_conversion_nuf }
};

static info_lkp_t mge_time_conversion[] = {
	{ 0, NULL, mge_time_conversion_fun, mge_time_conversion_nuf }
};
#else
static info_lkp_t mge_date_conversion[] = {
	{ 0, NULL, mge_date_conversion_fun, NULL }
};

static info_lkp_t mge_time_conversion[] = {
	{ 0, NULL, mge_time_conversion_fun, NULL }
};
#endif /* HAVE_STRPTIME */

/* The HID path 'UPS.PowerSummary.ConfigVoltage' only reports
   'battery.voltage.nominal' for specific UPS series. Ignore
   the value for other series (default behavior). */
static const char *mge_battery_voltage_nominal_fun(double value)
{
	switch (mge_type & 0xFF00)	/* Ignore model byte */
	{
	case MGE_EVOLUTION:
		if (mge_type == MGE_EVOLUTION_650) {
			value = 12.0;
		}
		break;

	case MGE_PULSAR_M:
		break;

	default:
		return NULL;
	}

	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.0f", value);
	return mge_scratch_buf;
}

static info_lkp_t mge_battery_voltage_nominal[] = {
	{ 0, NULL, mge_battery_voltage_nominal_fun }
};

/* The HID path 'UPS.PowerSummary.Voltage' only reports
   'battery.voltage' for specific UPS series. Ignore the
   value for other series (default behavior). */
static const char *mge_battery_voltage_fun(double value)
{
	switch (mge_type & 0xFF00)	/* Ignore model byte */
	{
	case MGE_EVOLUTION:
	case MGE_PULSAR_M:
		break;

	default:
		return NULL;
	}

	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.1f", value);
	return mge_scratch_buf;
}

static info_lkp_t mge_battery_voltage[] = {
	{ 0, NULL, mge_battery_voltage_fun }
};

static const char *mge_powerfactor_conversion_fun(double value)
{
	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.2f", value / 100);
	return mge_scratch_buf;
}

static info_lkp_t mge_powerfactor_conversion[] = {
	{ 0, NULL, mge_powerfactor_conversion_fun }
};

static const char *mge_battery_capacity_fun(double value)
{
	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.2f", value / 3600);
	return mge_scratch_buf;
}

static info_lkp_t mge_battery_capacity[] = {
	{ 0, NULL, mge_battery_capacity_fun }
};

static info_lkp_t mge_upstype_conversion[] = {
	{ 1, "offline / line interactive", NULL },
	{ 2, "online", NULL },
	{ 3, "online - unitary/parallel", NULL },
	{ 4, "online - parallel with hot standy", NULL },
	{ 5, "online - hot standby redundancy", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_sensitivity_info[] = {
	{ 0, "normal", NULL },
	{ 1, "high", NULL },
	{ 2, "low", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_emergency_stop[] = {
	{ 1, "Emergency stop!", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_wiring_fault[] = {
	{ 1, "Wiring fault!", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_config_failure[] = {
	{ 1, "Fatal EEPROM fault!", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_inverter_volthi[] = {
	{ 1, "Inverter AC voltage too high!", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_inverter_voltlo[] = {
	{ 1, "Inverter AC voltage too low!", NULL },
	{ 0, NULL, NULL }
};

static info_lkp_t mge_short_circuit[] = {
	{ 1, "Output short circuit!", NULL },
	{ 0, NULL, NULL }
};

info_lkp_t mge_onbatt_info[] = {
	{ 1, "!online", NULL },
	{ 0, "online", NULL },
	{ 0, NULL, NULL }
};

/* allow limiting to ups.model = Protection Station, Ellipse Eco
 * and 3S (US 750 and AUS 700 only!) */
static const char *eaton_check_pegasus_fun(double value)
{
	switch (mge_type & 0xFF00)	/* Ignore model byte */
	{
	case MGE_PEGASUS:
	case MGE_3S:
		break;
	default:
		return NULL;
	}

	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.0f", value);
	return mge_scratch_buf;
}

static info_lkp_t pegasus_threshold_info[] = {
	{ 10, "10", eaton_check_pegasus_fun },
	{ 25, "25", eaton_check_pegasus_fun },
	{ 60, "60", eaton_check_pegasus_fun },
	{ 0, NULL, NULL }
};

/* Limit nominal output voltage according to HV or LV models */
static const char *nominal_output_voltage_fun(double value)
{
	static long	nominal = -1;

	if (nominal < 0) {
		nominal = value;
	}

	switch ((long)nominal)
	{
	/* LV models */
	case 100:
	case 110:
	case 120:
	case 127:
		switch ((long)value)
		{
		case 100:
		case 110:
		case 120:
		case 127:
			break;
		default:
			return NULL;
		}
		break;

	/* HV models */
	/* 208V */
	case 200:
	case 208:
		switch ((long)value)
		{
		case 200:
		case 208:
			break;
		default:
			return NULL;
		}
		break;

	/* HV models */
	/* 230V */
	case 220:
	case 230:
	case 240:
		switch ((long)value)
		{
		case 220:
		case 230:
		case 240:
			break;
		default:
			return NULL;
		}
		break;

	default:
		upsdebugx(3, "%s: can't autodetect settable voltages from %g", __func__, value);
	}

	snprintf(mge_scratch_buf, sizeof(mge_scratch_buf), "%.0f", value);
	return mge_scratch_buf;
}

static info_lkp_t nominal_output_voltage_info[] = {
	/* HV models */
	/* 208V */
	{ 200, "200", nominal_output_voltage_fun },
	{ 208, "208", nominal_output_voltage_fun },
	/* HV models */
	/* 230V */
	{ 220, "220", nominal_output_voltage_fun },
	{ 230, "230", nominal_output_voltage_fun },
	{ 240, "240", nominal_output_voltage_fun },
	/* LV models */
	{ 100, "100", nominal_output_voltage_fun },
	{ 110, "110", nominal_output_voltage_fun },
	{ 120, "120", nominal_output_voltage_fun },
	{ 127, "127", nominal_output_voltage_fun },
	{ 0, NULL, NULL }
};

/* --------------------------------------------------------------- */
/*      Vendor-specific usage table */
/* --------------------------------------------------------------- */

/* MGE UPS SYSTEMS usage table */
static usage_lkp_t mge_usage_lkp[] = {
	{ "Undefined",				0xffff0000 },
	{ "STS",				0xffff0001 },
	{ "Environment",			0xffff0002 },
	{ "Statistic",			0xffff0003 },
	{ "StatisticSystem",		0xffff0004 },
	/* 0xffff0005-0xffff000f	=>	Reserved */
	{ "Phase",				0xffff0010 },
	{ "PhaseID",				0xffff0011 },
	{ "Chopper",				0xffff0012 },
	{ "ChopperID",				0xffff0013 },
	{ "Inverter",				0xffff0014 },
	{ "InverterID",				0xffff0015 },
	{ "Rectifier",				0xffff0016 },
	{ "RectifierID",			0xffff0017 },
	{ "LCMSystem",				0xffff0018 },
	{ "LCMSystemID",			0xffff0019 },
	{ "LCMAlarm",				0xffff001a },
	{ "LCMAlarmID",				0xffff001b },
	{ "HistorySystem",			0xffff001c },
	{ "HistorySystemID",			0xffff001d },
	{ "Event",				0xffff001e },
	{ "EventID",				0xffff001f },
	{ "CircuitBreaker",			0xffff0020 },
	{ "TransferForbidden",			0xffff0021 },
	{ "OverallAlarm",			0xffff0022 },
	{ "Dephasing",				0xffff0023 },
	{ "BypassBreaker",			0xffff0024 },
	{ "PowerModule",			0xffff0025 },
	{ "PowerRate",				0xffff0026 },
	{ "PowerSource",			0xffff0027 },
	{ "CurrentPowerSource",			0xffff0028 },
	{ "RedundancyLevel",			0xffff0029 },
	{ "RedundancyLost",			0xffff002a },
	{ "NotificationStatus",			0xffff002b },
	{ "ProtectionLost",			0xffff002c },
	{ "ConfigurationFailure",			0xffff002d },
	/* 0xffff002e-0xffff003f	=>	Reserved */
	{ "SwitchType",				0xffff0040 },
	{ "ConverterType",			0xffff0041 },
	{ "FrequencyConverterMode",		0xffff0042 },
	{ "AutomaticRestart",			0xffff0043 },
	{ "ForcedReboot",			0xffff0044 },
	{ "TestPeriod",				0xffff0045 },
	{ "EnergySaving",			0xffff0046 },
	{ "StartOnBattery",			0xffff0047 },
	{ "Schedule",				0xffff0048 },
	{ "DeepDischargeProtection",		0xffff0049 },
	{ "ShortCircuit",			0xffff004a },
	{ "ExtendedVoltageMode",		0xffff004b },
	{ "SensitivityMode",			0xffff004c },
	{ "RemainingCapacityLimitSetting",	0xffff004d },
	{ "ExtendedFrequencyMode",		0xffff004e },
	{ "FrequencyConverterModeSetting",	0xffff004f },
	{ "LowVoltageBoostTransfer",		0xffff0050 },
	{ "HighVoltageBoostTransfer",		0xffff0051 },
	{ "LowVoltageBuckTransfer",		0xffff0052 },
	{ "HighVoltageBuckTransfer",		0xffff0053 },
	{ "OverloadTransferEnable",		0xffff0054 },
	{ "OutOfToleranceTransferEnable",	0xffff0055 },
	{ "ForcedTransferEnable",		0xffff0056 },
	{ "LowVoltageBypassTransfer",		0xffff0057 },
	{ "HighVoltageBypassTransfer",		0xffff0058 },
	{ "FrequencyRangeBypassTransfer",	0xffff0059 },
	{ "LowVoltageEcoTransfer",		0xffff005a },
	{ "HighVoltageEcoTransfer",		0xffff005b },
	{ "FrequencyRangeEcoTransfer",		0xffff005c },
	{ "ShutdownTimer",			0xffff005d },
	{ "StartupTimer",			0xffff005e },
	{ "RestartLevel",			0xffff005f },
	{ "PhaseOutOfRange", 			0xffff0060 },
	{ "CurrentLimitation", 			0xffff0061 },
	{ "ThermalOverload", 			0xffff0062 },
	{ "SynchroSource", 			0xffff0063 },
	{ "FuseFault", 				0xffff0064 },
	{ "ExternalProtectedTransfert", 	0xffff0065 },
	{ "ExternalForcedTransfert", 		0xffff0066 },
	{ "Compensation", 			0xffff0067 },
	{ "EmergencyStop", 			0xffff0068 },
	{ "PowerFactor", 			0xffff0069 },
	{ "PeakFactor", 			0xffff006a },
	{ "ChargerType", 			0xffff006b },
	{ "HighPositiveDCBusVoltage", 		0xffff006c },
	{ "LowPositiveDCBusVoltage", 		0xffff006d },
	{ "HighNegativeDCBusVoltage", 		0xffff006e },
	{ "LowNegativeDCBusVoltage", 		0xffff006f },
	{ "FrequencyRangeTransfer", 		0xffff0070 },
	{ "WiringFaultDetection", 		0xffff0071 },
	{ "ControlStandby", 			0xffff0072 },
	{ "ShortCircuitTolerance", 		0xffff0073 },
	{ "VoltageTooHigh", 			0xffff0074 },
	{ "VoltageTooLow", 			0xffff0075 },
	{ "DCBusUnbalanced", 			0xffff0076 },
	{ "FanFailure", 			0xffff0077 },
	{ "WiringFault", 			0xffff0078 },
	{ "Floating", 			0xffff0079 },
	{ "OverCurrent", 			0xffff007a },
	{ "RemainingActivePower", 			0xffff007b },
	{ "Energy", 			0xffff007c },
	{ "Threshold", 			0xffff007d },
	{ "OverThreshold", 			0xffff007e },
	/* 0xffff007f	=>	Reserved */
	{ "Sensor",				0xffff0080 },
	{ "LowHumidity",			0xffff0081 },
	{ "HighHumidity",			0xffff0082 },
	{ "LowTemperature",			0xffff0083 },
	{ "HighTemperature",			0xffff0084 },
	/* 0xffff0085-0xffff008f (minus 0xffff0086)	=>	Reserved */
	{ "Efficiency",			0xffff0086 },
	{ "Count",				0xffff0090 },
	{ "Timer",				0xffff0091 },
	{ "Interval",				0xffff0092 },
	{ "TimerExpired",			0xffff0093 },
	{ "Mode",				0xffff0094 },
	{ "Country",				0xffff0095 },
	{ "State",				0xffff0096 },
	{ "Time",				0xffff0097 },
	{ "Code",				0xffff0098 },
	{ "DataValid",				0xffff0099 },
	{ "ToggleTimer",				0xffff009a },
	/* 0xffff009b-0xffff009f	=>	Reserved */
	{ "PDU",				0xffff00a0 },
	{ "Breaker",				0xffff00a1 },
	{ "BreakerID",				0xffff00a2 },
	{ "OverVoltage",				0xffff00a3 },
	{ "Tripped",				0xffff00a4 },
	{ "OverEnergy",				0xffff00a5 },
	{ "OverHumidity",				0xffff00a6 },
	{ "LCDControl",				0xffff00a6 },
	/* 0xffff00a8-0xffff00df	=>	Reserved */
	{ "COPIBridge",				0xffff00e0 },
	/* 0xffff00e1-0xffff00ef	=>	Reserved */
	{ "iModel",				0xffff00f0 },
	{ "iVersion",				0xffff00f1 },
	{ "iTechnicalLevel",		0xffff00f2 },
	{ "iPartNumber",			0xffff00f3 },
	{ "iReferenceNumber",		0xffff00f4 },
	/* 0xffff00f5-0xffff00ff	=>	Reserved */

	/* end of table */
	{ NULL, 0 }
};

static usage_tables_t mge_utab[] = {
	mge_usage_lkp,
	hid_usage_lkp,
	NULL,
};


/* --------------------------------------------------------------- */
/*      Model Name formating entries                               */
/* --------------------------------------------------------------- */

typedef struct {
	const char	*iProduct;
	const char	*iModel;
	models_type_t	type;	/* enumerated model type */
	const char	*name;		/* optional (defaults to "<iProduct> <iModel>" if NULL) */
} models_name_t;

/*
 * Do not remove models from this list, but instead comment them
 * out if not needed. This allows us to quickly add overrides for
 * specific models only, should this be needed.
 */
static models_name_t mge_model_names [] =
{
	/* Ellipse models */
	{ "ELLIPSE", "300", MGE_DEFAULT, "ellipse 300" },
	{ "ELLIPSE", "500", MGE_DEFAULT, "ellipse 500" },
	{ "ELLIPSE", "650", MGE_DEFAULT, "ellipse 650" },
	{ "ELLIPSE", "800", MGE_DEFAULT, "ellipse 800" },
	{ "ELLIPSE", "1200", MGE_DEFAULT, "ellipse 1200" },

	/* Ellipse Premium models */
	{ "ellipse", "PR500", MGE_DEFAULT, "ellipse premium 500" },
	{ "ellipse", "PR650", MGE_DEFAULT, "ellipse premium 650" },
	{ "ellipse", "PR800", MGE_DEFAULT, "ellipse premium 800" },
	{ "ellipse", "PR1200", MGE_DEFAULT, "ellipse premium 1200" },

	/* Ellipse "Pro" */
	{ "ELLIPSE", "600", MGE_DEFAULT, "Ellipse 600" },
	{ "ELLIPSE", "750", MGE_DEFAULT, "Ellipse 750" },
	{ "ELLIPSE", "1000", MGE_DEFAULT, "Ellipse 1000" },
	{ "ELLIPSE", "1500", MGE_DEFAULT, "Ellipse 1500" },

	/* Ellipse "MAX" (TBR) */
/*	{ "Ellipse MAX", "600", MGE_DEFAULT, NULL }, */
/*	{ "Ellipse MAX", "850", MGE_DEFAULT, NULL }, */
/*	{ "Ellipse MAX", "1100", MGE_DEFAULT, NULL }, */
/*	{ "Ellipse MAX", "1500", MGE_DEFAULT, NULL }, */

	/* Protection Center */
	{ "PROTECTIONCENTER", "420", MGE_DEFAULT, "Protection Center 420" },
	{ "PROTECTIONCENTER", "500", MGE_DEFAULT, "Protection Center 500" },
	{ "PROTECTIONCENTER", "675", MGE_DEFAULT, "Protection Center 675" },

	/* Protection Station, supports Eco control */
	{ "Protection Station", "500", MGE_PEGASUS, NULL },
	{ "Protection Station", "650", MGE_PEGASUS, NULL },
	{ "Protection Station", "800", MGE_PEGASUS, NULL },

	/* Ellipse ECO, also supports Eco control */
	{ "Ellipse ECO", "650", MGE_PEGASUS, NULL },
	{ "Ellipse ECO", "800", MGE_PEGASUS, NULL },
	{ "Ellipse ECO", "1200", MGE_PEGASUS, NULL },
	{ "Ellipse ECO", "1600", MGE_PEGASUS, NULL },

	/* 3S, also supports Eco control on some models (AUS 700 and US 750)*/
	{ "3S", "450", MGE_DEFAULT, NULL }, /* US only */
	{ "3S", "550", MGE_DEFAULT, NULL }, /* US 120V + EU 230V + AUS 240V */
	{ "3S", "700", MGE_3S, NULL }, /* EU 230V + AUS 240V (w/ eco control) */
	{ "3S", "750", MGE_3S, NULL }, /* US 120V (w/ eco control) */

	/* Evolution models */
	{ "Evolution", "500", MGE_DEFAULT, "Pulsar Evolution 500" },
	{ "Evolution", "800", MGE_DEFAULT, "Pulsar Evolution 800" },
	{ "Evolution", "1100", MGE_DEFAULT, "Pulsar Evolution 1100" },
	{ "Evolution", "1500", MGE_DEFAULT, "Pulsar Evolution 1500" },
	{ "Evolution", "2200", MGE_DEFAULT, "Pulsar Evolution 2200" },
	{ "Evolution", "3000", MGE_DEFAULT, "Pulsar Evolution 3000" },
	{ "Evolution", "3000XL", MGE_DEFAULT, "Pulsar Evolution 3000 XL" },

	/* Newer Evolution models */
	{ "Evolution", "650", MGE_EVOLUTION_650, NULL },
	{ "Evolution", "850", MGE_EVOLUTION_850, NULL },
	{ "Evolution", "1150", MGE_EVOLUTION_1150, NULL },
	{ "Evolution", "S 1250", MGE_EVOLUTION_S_1250, NULL },
	{ "Evolution", "1550", MGE_EVOLUTION_1550, NULL },
	{ "Evolution", "S 1750", MGE_EVOLUTION_S_1750, NULL },
	{ "Evolution", "2000", MGE_EVOLUTION_2000, NULL },
	{ "Evolution", "S 2500", MGE_EVOLUTION_S_2500, NULL },
	{ "Evolution", "S 3000", MGE_EVOLUTION_S_3000, NULL },

	/* Pulsar M models */
	{ "PULSAR M", "2200", MGE_PULSAR_M_2200, NULL },
	{ "PULSAR M", "3000", MGE_PULSAR_M_3000, NULL },
	{ "PULSAR M", "3000 XL", MGE_PULSAR_M_3000_XL, NULL },
	/* Eaton'ified names */
	{ "EX", "2200", MGE_PULSAR_M_2200, NULL },
	{ "EX", "3000", MGE_PULSAR_M_3000, NULL },
	{ "EX", "3000 XL", MGE_PULSAR_M_3000, NULL },

	/* Pulsar models (TBR) */
/*	{ "Pulsar", "700", MGE_DEFAULT, NULL }, */
/*	{ "Pulsar", "1000", MGE_DEFAULT, NULL }, */
/*	{ "Pulsar", "1500", MGE_DEFAULT, NULL }, */
/*	{ "Pulsar", "1000 RT2U", MGE_DEFAULT, NULL }, */
/*	{ "Pulsar", "1500 RT2U", MGE_DEFAULT, NULL }, */
	/* Eaton'ified names (TBR) */
/*	{ "EX", "700", MGE_DEFAULT, NULL }, */
/*	{ "EX", "1000", MGE_DEFAULT, NULL }, */
/*	{ "EX", "1500", MGE_DEFAULT, NULL }, */
/*	{ "EX", "1000 RT2U", MGE_DEFAULT, NULL }, */
/*	{ "EX", "1500 RT2U", MGE_DEFAULT, NULL }, */

	/* Pulsar MX models */
	{ "PULSAR", "MX4000", MGE_DEFAULT, "Pulsar MX 4000 RT" },
	{ "PULSAR", "MX5000", MGE_DEFAULT, "Pulsar MX 5000 RT" },

	/* NOVA models */
	{ "NOVA AVR", "500", MGE_DEFAULT, "Nova 500 AVR" },
	{ "NOVA AVR", "600", MGE_DEFAULT, "Nova 600 AVR" },
	{ "NOVA AVR", "625", MGE_DEFAULT, "Nova 625 AVR" },
	{ "NOVA AVR", "1100", MGE_DEFAULT, "Nova 1100 AVR" },
	{ "NOVA AVR", "1250", MGE_DEFAULT, "Nova 1250 AVR" },

	/* EXtreme C (EMEA) */
	{ "EXtreme", "700C", MGE_DEFAULT, "Pulsar EXtreme 700C" },
	{ "EXtreme", "1000C", MGE_DEFAULT, "Pulsar EXtreme 1000C" },
	{ "EXtreme", "1500C", MGE_DEFAULT, "Pulsar EXtreme 1500C" },
	{ "EXtreme", "1500CCLA", MGE_DEFAULT, "Pulsar EXtreme 1500C CLA" },
	{ "EXtreme", "2200C", MGE_DEFAULT, "Pulsar EXtreme 2200C" },
	{ "EXtreme", "3200C", MGE_DEFAULT, "Pulsar EXtreme 3200C" },

	/* EXtreme C (USA, aka "EX RT") */
	{ "EX", "700RT", MGE_DEFAULT, "Pulsar EX 700 RT" },
	{ "EX", "1000RT", MGE_DEFAULT, "Pulsar EX 1000 RT" },
	{ "EX", "1500RT", MGE_DEFAULT, "Pulsar EX 1500 RT" },
	{ "EX", "2200RT", MGE_DEFAULT, "Pulsar EX 2200 RT" },
	{ "EX", "3200RT", MGE_DEFAULT, "Pulsar EX 3200 RT" },

	/* Comet EX RT three phased */
	{ "EX", "5RT31", MGE_DEFAULT, "EX 5 RT 3:1" },
	{ "EX", "7RT31", MGE_DEFAULT, "EX 7 RT 3:1" },
	{ "EX", "11RT31", MGE_DEFAULT, "EX 11 RT 3:1" },

	/* Comet EX RT mono phased */
	{ "EX", "5RT", MGE_DEFAULT, "EX 5 RT" },
	{ "EX", "7RT", MGE_DEFAULT, "EX 7 RT" },
	{ "EX", "11RT", MGE_DEFAULT, "EX 11 RT" },

	/* Galaxy 3000 */
	{ "GALAXY", "3000_10", MGE_DEFAULT, "Galaxy 3000 10 kVA" },
	{ "GALAXY", "3000_15", MGE_DEFAULT, "Galaxy 3000 15 kVA" },
	{ "GALAXY", "3000_20", MGE_DEFAULT, "Galaxy 3000 20 kVA" },
	{ "GALAXY", "3000_30", MGE_DEFAULT, "Galaxy 3000 30 kVA" },

	/* end of structure. */
	{ NULL }
};


/* --------------------------------------------------------------- */
/*                 Data lookup table (HID <-> NUT)                 */
/* --------------------------------------------------------------- */

static hid_info_t mge_hid2nut[] =
{
	/* Battery page */
	{ "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", NULL, "%.0f", 0, NULL },
	{ "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerSummary.RemainingCapacityLimitSetting", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "battery.charge.low", 0, 0, "UPS.PowerSummary.RemainingCapacityLimit", NULL, "%.0f", HU_FLAG_STATIC , NULL }, /* Read only */
	{ "battery.charge.restart", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.PowerSummary.RestartLevel", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "battery.capacity", 0, 0, "UPS.BatterySystem.Battery.DesignCapacity", NULL, "%s", HU_FLAG_STATIC, mge_battery_capacity },	/* conversion needed from As to Ah */
	{ "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", NULL, "%.0f", 0, NULL },
	{ "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", NULL, "%.0f", 0, NULL },
	{ "battery.runtime.elapsed", 0, 0, "UPS.StatisticSystem.Input.[1].Statistic.[1].Time", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL },
	{ "battery.temperature", 0, 0, "UPS.BatterySystem.Battery.Temperature", NULL, "%s", 0, kelvin_celsius_conversion },
	{ "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", NULL, "%s", HU_FLAG_STATIC, stringid_conversion },
	{ "battery.voltage", 0, 0, "UPS.BatterySystem.Voltage", NULL, "%.1f", 0, NULL },
	{ "battery.voltage", 0, 0, "UPS.PowerSummary.Voltage", NULL, "%s", 0, mge_battery_voltage },
	{ "battery.voltage.nominal", 0, 0, "UPS.BatterySystem.ConfigVoltage", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "battery.voltage.nominal", 0, 0, "UPS.PowerSummary.ConfigVoltage", NULL, "%s", HU_FLAG_STATIC, mge_battery_voltage_nominal },
	{ "battery.protection", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.BatterySystem.Battery.DeepDischargeProtection", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
	{ "battery.energysave", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].EnergySaving", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },

	/* UPS page */
	{ "ups.efficiency", 0, 0, "UPS.PowerConverter.Output.Efficiency", NULL, "%.0f", 0, NULL },
	{ "ups.firmware", 0, 0, "UPS.PowerSummary.iVersion", NULL, "%s", HU_FLAG_STATIC, stringid_conversion },
	{ "ups.load", 0, 0, "UPS.PowerSummary.PercentLoad", NULL, "%.0f", 0, NULL },
	{ "ups.load.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.Flow.[4].ConfigPercentLoad", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "ups.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.DelayBeforeStartup", NULL, DEFAULT_ONDELAY, HU_FLAG_ABSENT, NULL},
	{ "ups.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.DelayBeforeShutdown", NULL, DEFAULT_OFFDELAY, HU_FLAG_ABSENT, NULL},
	{ "ups.timer.start", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL},
	{ "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL},
	{ "ups.timer.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL},
	{ "ups.test.result", 0, 0, "UPS.BatterySystem.Battery.Test", NULL, "%s", 0, test_read_info },
	{ "ups.test.interval", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.BatterySystem.Battery.TestPeriod", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "ups.beeper.status", 0 ,0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "%s", HU_FLAG_SEMI_STATIC, beeper_info },
	{ "ups.temperature", 0, 0, "UPS.PowerSummary.Temperature", NULL, "%s", 0, kelvin_celsius_conversion },
	{ "ups.power", 0, 0, "UPS.PowerConverter.Output.ApparentPower", NULL, "%.0f", 0, NULL },
	{ "ups.L1.power", 0, 0, "UPS.PowerConverter.Output.Phase.[1].ApparentPower", NULL, "%.0f", 0, NULL },
	{ "ups.L2.power", 0, 0, "UPS.PowerConverter.Output.Phase.[2].ApparentPower", NULL, "%.0f", 0, NULL },
	{ "ups.L3.power", 0, 0, "UPS.PowerConverter.Output.Phase.[3].ApparentPower", NULL, "%.0f", 0, NULL },
	{ "ups.power.nominal", 0, 0, "UPS.Flow.[4].ConfigApparentPower", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "ups.realpower", 0, 0, "UPS.PowerConverter.Output.ActivePower", NULL, "%.0f", 0, NULL },
	{ "ups.L1.realpower", 0, 0, "UPS.PowerConverter.Output.Phase.[1].ActivePower", NULL, "%.0f", 0, NULL },
	{ "ups.L2.realpower", 0, 0, "UPS.PowerConverter.Output.Phase.[2].ActivePower", NULL, "%.0f", 0, NULL },
	{ "ups.L3.realpower", 0, 0, "UPS.PowerConverter.Output.Phase.[3].ActivePower", NULL, "%.0f", 0, NULL },
	{ "ups.realpower.nominal", 0, 0, "UPS.Flow.[4].ConfigActivePower", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "ups.start.auto", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[1].AutomaticRestart", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
	{ "ups.start.battery", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].StartOnBattery", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
	{ "ups.start.reboot", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.ForcedReboot", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
#ifdef HAVE_STRPTIME
	{ "ups.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.Time", NULL, "%s", 0, mge_date_conversion },
	{ "ups.time", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.Time", NULL, "%s", 0, mge_time_conversion },
#else
	{ "ups.date", 0, 0, "UPS.PowerSummary.Time", NULL, "%s", 0, mge_date_conversion },
	{ "ups.time", 0, 0, "UPS.PowerSummary.Time", NULL, "%s", 0, mge_time_conversion },
#endif /* HAVE_STRPTIME */
	{ "ups.type", 0, 0, "UPS.PowerConverter.ConverterType", NULL, "%s", HU_FLAG_STATIC, mge_upstype_conversion },

	/* Special case: boolean values that are mapped to ups.status and ups.alarm */
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", NULL, NULL, HU_FLAG_QUICK_POLL, online_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[3].PresentStatus.Used", NULL, NULL, 0, mge_onbatt_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Used", NULL, NULL, 0, online_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", NULL, NULL, HU_FLAG_QUICK_POLL, discharging_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", NULL, NULL, HU_FLAG_QUICK_POLL, charging_info },
	/* FIXME: on Dell, the above requires an "AND" with "UPS.BatterySystem.Charger.Mode = 1" */
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", NULL, NULL, HU_FLAG_QUICK_POLL, lowbatt_info },
	/* Output overload, Level 1 (FIXME: add the level?) */
	{ "BOOL", 0, 0, "UPS.PowerConverter.Output.Overload.[1].PresentStatus.OverThreshold", NULL, NULL, 0, overload_info },
	/* Output overload, Level 2 (FIXME: add the level?) */
	{ "BOOL", 0, 0, "UPS.PowerConverter.Output.Overload.[2].PresentStatus.OverThreshold", NULL, NULL, 0, overload_info },
	/* Output overload, Level 3 (FIXME: add the level?) */
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Overload", NULL, NULL, 0, overload_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", NULL, NULL, 0, replacebatt_info },
	/* FIXME: on Dell, the above requires an "AND" with "UPS.BatterySystem.Battery.Test = 3 " */
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Buck", NULL, NULL, 0, trim_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Boost", NULL, NULL, 0, boost_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.VoltageOutOfRange", NULL, NULL, 0, vrange_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.FrequencyOutOfRange", NULL, NULL, 0, frange_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Good", NULL, NULL, 0, off_info },
	{ "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.Used", NULL, NULL, 0, off_info },
	/* FIXME: on Dell, the above requires an "AND" with "UPS.BatterySystem.Charger.Mode = 4 (ABM Resting)" */
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[2].PresentStatus.Used", NULL, NULL, 0, bypass_auto_info }, /* Automatic bypass */
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[4].PresentStatus.Used", NULL, NULL, 0, bypass_manual_info }, /* Manual bypass */
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.FanFailure", NULL, NULL, 0, fanfail_info },
	{ "BOOL", 0, 0, "UPS.BatterySystem.Battery.PresentStatus.Present", NULL, NULL, 0, nobattery_info },
	{ "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.InternalFailure", NULL, NULL, 0, chargerfail_info },
	{ "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.VoltageTooHigh", NULL, NULL, 0, battvolthi_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.VoltageTooHigh", NULL, NULL, 0, battvolthi_info },
	/* Battery DC voltage too high! */
	{ "BOOL", 0, 0, "UPS.BatterySystem.Battery.PresentStatus.VoltageTooHigh", NULL, NULL, 0, battvolthi_info },
	{ "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.VoltageTooLow", NULL, NULL, 0, battvoltlo_info },
	{ "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.VoltageTooLow", NULL, NULL, 0, mge_onbatt_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.InternalFailure", NULL, NULL, 0, commfault_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.OverTemperature", NULL, NULL, 0, overheat_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ShutdownImminent", NULL, NULL, 0, shutdownimm_info },

	/* Vendor specific ups.alarm */
	{ "ups.alarm", 0, 0, "UPS.PowerSummary.PresentStatus.EmergencyStop", NULL, NULL, 0, mge_emergency_stop },
	{ "ups.alarm", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.WiringFault", NULL, NULL, 0, mge_wiring_fault },
	{ "ups.alarm", 0, 0, "UPS.PowerSummary.PresentStatus.ConfigurationFailure", NULL, NULL, 0, mge_config_failure },
	{ "ups.alarm", 0, 0, "UPS.PowerConverter.Inverter.PresentStatus.VoltageTooHigh", NULL, NULL, 0, mge_inverter_volthi },
	{ "ups.alarm", 0, 0, "UPS.PowerConverter.Inverter.PresentStatus.VoltageTooLow", NULL, NULL, 0, mge_inverter_voltlo },
	{ "ups.alarm", 0, 0, "UPS.PowerConverter.Output.PresentStatus.ShortCircuit", NULL, NULL, 0, mge_short_circuit },

	/* Input page */
	{ "input.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L1-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[1].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L2-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[2].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L3-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[3].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[12].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[23].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[31].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.voltage.nominal", 0, 0, "UPS.Flow.[1].ConfigVoltage", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "input.current", 0, 0, "UPS.PowerConverter.Input.[1].Current", NULL, "%.2f", 0, NULL },
	{ "input.L1.current", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[1].Current", NULL, "%.1f", 0, NULL },
	{ "input.L2.current", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[2].Current", NULL, "%.1f", 0, NULL },
	{ "input.L3.current", 0, 0, "UPS.PowerConverter.Input.[1].Phase.[3].Current", NULL, "%.1f", 0, NULL },
	{ "input.current.nominal", 0, 0, "UPS.Flow.[1].ConfigCurrent", NULL, "%.2f", HU_FLAG_STATIC, NULL },
	{ "input.frequency", 0, 0, "UPS.PowerConverter.Input.[1].Frequency", NULL, "%.1f", 0, NULL },
	{ "input.frequency.nominal", 0, 0, "UPS.Flow.[1].ConfigFrequency", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	/* same as "input.transfer.boost.low" */
	{ "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.LowVoltageTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "input.transfer.boost.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.LowVoltageBoostTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "input.transfer.boost.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.HighVoltageBoostTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "input.transfer.trim.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.LowVoltageBuckTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	/* same as "input.transfer.trim.high" */
	{ "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.HighVoltageTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "input.transfer.trim.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.HighVoltageBuckTransfer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerConverter.Output.SensitivityMode", NULL, "%s", HU_FLAG_SEMI_STATIC, mge_sensitivity_info },
	{ "input.voltage.extended", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.ExtendedVoltageMode", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
	{ "input.frequency.extended", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.ExtendedFrequencyMode", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },

	/* Bypass page */
	{ "input.bypass.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L1-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[1].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L2-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[2].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L3-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[3].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[12].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[23].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[31].Voltage", NULL, "%.1f", 0, NULL },
	{ "input.bypass.voltage.nominal", 0, 0, "UPS.Flow.[2].ConfigVoltage", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "input.bypass.current", 0, 0, "UPS.PowerConverter.Input.[2].Current", NULL, "%.2f", 0, NULL },
	{ "input.bypass.L1.current", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[1].Current", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L2.current", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[2].Current", NULL, "%.1f", 0, NULL },
	{ "input.bypass.L3.current", 0, 0, "UPS.PowerConverter.Input.[2].Phase.[3].Current", NULL, "%.1f", 0, NULL },
	{ "input.bypass.current.nominal", 0, 0, "UPS.Flow.[2].ConfigCurrent", NULL, "%.2f", HU_FLAG_STATIC, NULL },
	{ "input.bypass.frequency", 0, 0, "UPS.PowerConverter.Input.[2].Frequency", NULL, "%.1f", 0, NULL },
	{ "input.bypass.frequency.nominal", 0, 0, "UPS.Flow.[2].ConfigFrequency", NULL, "%.0f", HU_FLAG_STATIC, NULL },

	/* Output page */
	{ "output.voltage", 0, 0, "UPS.PowerConverter.Output.Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L1-N.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[1].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L2-N.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[2].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L3-N.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[3].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[12].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[23].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Output.Phase.[31].Voltage", NULL, "%.1f", 0, NULL },
	{ "output.voltage.nominal", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.Flow.[4].ConfigVoltage", NULL, "%.0f", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, nominal_output_voltage_info },
	{ "output.current", 0, 0, "UPS.PowerConverter.Output.Current", NULL, "%.2f", 0, NULL },
	{ "output.L1.current", 0, 0, "UPS.PowerConverter.Output.Phase.[1].Current", NULL, "%.1f", 0, NULL },
	{ "output.L2.current", 0, 0, "UPS.PowerConverter.Output.Phase.[2].Current", NULL, "%.1f", 0, NULL },
	{ "output.L3.current", 0, 0, "UPS.PowerConverter.Output.Phase.[3].Current", NULL, "%.1f", 0, NULL },
	{ "output.current.nominal", 0, 0, "UPS.Flow.[4].ConfigCurrent", NULL, "%.2f", 0, NULL },
	{ "output.frequency", 0, 0, "UPS.PowerConverter.Output.Frequency", NULL, "%.1f", 0, NULL },
	/* FIXME: can be RW (50/60) (or on .nominal)? */
	{ "output.frequency.nominal", 0, 0, "UPS.Flow.[4].ConfigFrequency", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "output.powerfactor", 0, 0, "UPS.PowerConverter.Output.PowerFactor", NULL, "%s", 0, mge_powerfactor_conversion },

	/* Outlet page (using MGE UPS SYSTEMS - PowerShare technology) */
	{ "outlet.id", 0, 0, "UPS.OutletSystem.Outlet.[1].OutletID", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "outlet.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[1].OutletID", NULL, "Main Outlet", HU_FLAG_ABSENT, NULL },
	{ "outlet.switchable", 0, 0, "UPS.OutletSystem.Outlet.[1].PresentStatus.Switchable", NULL, "%s", HU_FLAG_STATIC, yes_no_info },
	/* On Protection Station, the line below is the power consumption threshold
	 * on the master outlet used to automatically power off the slave outlets.
	 * Values: 10, 25 (default) or 60 VA. */
	{ "outlet.power", ST_FLAG_RW | ST_FLAG_STRING, 6, "UPS.OutletSystem.Outlet.[1].ConfigApparentPower", NULL, "%s", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, pegasus_threshold_info },
	{ "outlet.1.id", 0, 0, "UPS.OutletSystem.Outlet.[2].OutletID", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "outlet.1.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[2].OutletID", NULL, "PowerShare Outlet 1", HU_FLAG_ABSENT, NULL },
	{ "outlet.1.switchable", 0, 0, "UPS.OutletSystem.Outlet.[2].PresentStatus.Switchable", NULL, "%s", HU_FLAG_STATIC, yes_no_info },
	{ "outlet.1.status", 0, 0, "UPS.OutletSystem.Outlet.[2].PresentStatus.SwitchOn/Off", NULL, "%s", 0, on_off_info },
	/* For low end models, with 1 non backup'ed outlet */
	{ "outlet.1.status", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", NULL, "%s", 0, on_off_info },
	{ "outlet.1.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[2].RemainingCapacityLimit", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "outlet.1.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].ShutdownTimer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "outlet.1.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].StartupTimer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "outlet.2.id", 0, 0, "UPS.OutletSystem.Outlet.[3].OutletID", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "outlet.2.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[3].OutletID", NULL, "PowerShare Outlet 2", HU_FLAG_ABSENT, NULL },
	/* needed for Pegasus to enable master/slave mode */
	{ "outlet.2.switchable", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable", NULL, "%s", HU_FLAG_SEMI_STATIC, yes_no_info },
	{ "outlet.2.status", 0, 0, "UPS.OutletSystem.Outlet.[3].PresentStatus.SwitchOn/Off", NULL, "%s", 0, on_off_info },
	{ "outlet.2.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].RemainingCapacityLimit", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "outlet.2.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].ShutdownTimer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },
	{ "outlet.2.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].StartupTimer", NULL, "%.0f", HU_FLAG_SEMI_STATIC, NULL },

	/* instant commands. */
	/* splited into subset while waiting for extradata support
	* ie: test.battery.start quick
	*/
	{ "test.battery.start.quick", 0, 0, "UPS.BatterySystem.Battery.Test", NULL, "1", HU_TYPE_CMD, NULL },
	{ "test.battery.start.deep", 0, 0, "UPS.BatterySystem.Battery.Test", NULL, "2", HU_TYPE_CMD, NULL },
	{ "test.battery.stop", 0, 0, "UPS.BatterySystem.Battery.Test", NULL, "3", HU_TYPE_CMD, NULL },
	{ "load.off.delay", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, DEFAULT_OFFDELAY, HU_TYPE_CMD, NULL },
	{ "load.on.delay", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", NULL, DEFAULT_ONDELAY, HU_TYPE_CMD, NULL },
	{ "shutdown.stop", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, "-1", HU_TYPE_CMD, NULL },
	{ "shutdown.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", NULL, "10", HU_TYPE_CMD, NULL},
	{ "beeper.off", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "1", HU_TYPE_CMD, NULL },
	{ "beeper.on", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "2", HU_TYPE_CMD, NULL },
	{ "beeper.mute", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "3", HU_TYPE_CMD, NULL },
	{ "beeper.disable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "1", HU_TYPE_CMD, NULL },
	{ "beeper.enable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", NULL, "2", HU_TYPE_CMD, NULL },

	/* Command for the outlet collection */
	{ "outlet.1.load.off", 0, 0, "UPS.OutletSystem.Outlet.[2].DelayBeforeShutdown", NULL, "0", HU_TYPE_CMD, NULL },
	{ "outlet.1.load.on", 0, 0, "UPS.OutletSystem.Outlet.[2].DelayBeforeStartup", NULL, "0", HU_TYPE_CMD, NULL },
	{ "outlet.2.load.off", 0, 0, "UPS.OutletSystem.Outlet.[3].DelayBeforeShutdown", NULL, "0", HU_TYPE_CMD, NULL },
	{ "outlet.2.load.on", 0, 0, "UPS.OutletSystem.Outlet.[3].DelayBeforeStartup", NULL, "0", HU_TYPE_CMD, NULL },

	/* end of structure. */
	{ NULL }
};

/*
 * All the logic for finely formatting the MGE model name and device
 * type matching (used for device specific values or corrections).
 * Returns pointer to (dynamically allocated) model name.
 */
static char *get_model_name(const char *iProduct, const char *iModel)
{
	models_name_t	*model = NULL;

	upsdebugx(2, "get_model_name(%s, %s)\n", iProduct, iModel);

	/* Search for device type and formatting rules */
	for (model = mge_model_names; model->iProduct; model++) {
		upsdebugx(2, "comparing with: %s", model->name);

		if (strcmp(iProduct, model->iProduct)) {
			continue;
		}

		if (strcmp(iModel, model->iModel)) {
			continue;
		}

		mge_type = model->type;
		break;
	}

	if (!model->name) {
		/*
		 * Model not found or NULL (use default) so construct
		 * model name by concatenation of iProduct and iModel
		 */
		char	buf[SMALLBUF];
		snprintf(buf, sizeof(buf), "%s %s", iProduct, iModel);
		return strdup(buf);
	}

	return strdup(model->name);
}

static const char *mge_format_model(HIDDevice_t *hd) {
	char	product[SMALLBUF];
	char	model[SMALLBUF];
	double	value;

	/* Dell has already a fully formatted name in iProduct */
	if (hd->VendorID == DELL_VENDORID) {
		return hd->Product;
	}

	/* Get iProduct and iModel strings */
	snprintf(product, sizeof(product), "%s", hd->Product ? hd->Product : "unknown");

	HIDGetItemString(udev, "UPS.PowerSummary.iModel", model, sizeof(model), mge_utab);

	/* Fallback to ConfigApparentPower */
	if ((strlen(model) < 1) && (HIDGetItemValue(udev, "UPS.Flow.[4].ConfigApparentPower", &value, mge_utab) == 1 )) {
		snprintf(model, sizeof(model), "%i", (int)value);
	}

	if (strlen(model) > 0) {
		free(hd->Product);
		hd->Product = get_model_name(product, model);
	}

	return hd->Product;
}

static const char *mge_format_mfr(HIDDevice_t *hd) {
	return hd->Vendor ? hd->Vendor : "Eaton";
}

static const char *mge_format_serial(HIDDevice_t *hd) {
	return hd->Serial;
}

/* this function allows the subdriver to "claim" a device: return 1 if
 * the device is supported by this subdriver, else 0. */
static int mge_claim(HIDDevice_t *hd) {

#ifndef SHUT_MODE
	int status = is_usb_device_supported(mge_usb_device_table, hd->VendorID,
								 hd->ProductID);

	switch (status) {

		case POSSIBLY_SUPPORTED:
			/* by default, reject, unless the productid option is given */
			if (getval("productid")) {
				return 1;
			}
			possibly_supported("Eaton / MGE", hd);
			return 0;

		case SUPPORTED:
			return 1;

		case NOT_SUPPORTED:
		default:
			return 0;
	}
#else
			return 1;
#endif
}

subdriver_t mge_subdriver = {
	MGE_HID_VERSION,
	mge_claim,
	mge_utab,
	mge_hid2nut,
	mge_format_model,
	mge_format_mfr,
	mge_format_serial,
};
