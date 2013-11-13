/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libecal/libecal.h>
#include <libsoup/soup-misc.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-message.h"
#include "server/e-ews-item-change.h"

#include "e-cal-backend-ews-utils.h"

/*
 * A bunch of global variables used to map the icaltimezone to MSDN[0] format.
 * Also, some auxiliar functions to translate from one tz type to another.
 *
 * [0]: http://msdn.microsoft.com/en-us/library/ms912391(v=winembedded.11).aspx
 */
static GRecMutex tz_mutex;

static GHashTable *ical_to_msdn = NULL;
static GHashTable *msdn_to_ical = NULL;

struct tz_map {
	const gchar *from;
	const gchar *to;
};

static const struct tz_map msdn_to_ical_table[] = {
	{ "Dateline Standard Time", "Pacific/Apia" },
	{ "Samoa Standard Time", "Pacific/Midway" },
	{ "Hawaiian Standard Time", "Pacific/Honolulu" },
	{ "Alaskan Standard Time", "America/Anchorage" },
	{ "Pacific Standard Time", "America/Los_Angeles" },
	{ "Pacific Standard Time (Mexico)", "America/Tijuana" },
	{ "US Mountain Standard Time", "America/Phoenix" },
	{ "Mountain Standard Time (Mexico)", "America/Mazatlan" },
	{ "Mexico Standard Time 2", "America/Chihuahua" },
	{ "Mountain Standard Time", "America/Denver" },
	{ "Central America Standard Time", "America/Costa_Rica" },
	{ "Central Standard Time", "America/Chicago" },
	{ "Central Standard Time (Mexico)", "America/Monterrey" },
	{ "Mexico Standard Time", "America/Mexico_City" },
	{ "Canada Central Standard Time", "America/Winnipeg" },
	{ "SA Pacific Standard Time", "America/Bogota" },
	{ "Eastern Standard Time", "America/New_York" },
	{ "US Eastern Standard Time", "America/Indiana/Indianapolis" },
	{ "Venezuela Standard Time", "America/Caracas" },
	{ "Atlantic Standard Time", "America/Halifax" },
	{ "SA Western Standard Time", "America/La_Paz" },
	{ "Central Brazilian Standard Time", "America/Manaus" },
	{ "Pacific SA Standard Time", "America/La_Paz" },
	{ "Newfoundland Standard Time", "America/St_Johns" },
	{ "E. South America Standard Time", "America/Bahia" },
	{ "SA Eastern Standard Time", "America/Argentina/Buenos_Aires" },
	{ "Greenland Standard Time", "America/Godthab" },
	{ "Montevideo Standard Time", "America/Montevideo" },
	{ "Mid-Atlantic Standard Time", "Atlantic/South_Georgia" },
	{ "Azores Standard Time", "Atlantic/Azores" },
	{ "Cape Verde Standard Time", "Atlantic/Cape_Verde" },
	{ "Greenwich Standard Time", "Africa/Casablanca" },
	{ "GMT Standard Time", "Europe/Dublin" },
	{ "UTC", "UTC" },
	{ "W. Europe Standard Time", "Europe/Berlin" },
	{ "Central Europe Standard Time", "Europe/Prague" },
	{ "Romance Standard Time", "Europe/Paris" },
	{ "Central European Standard Time", "Europe/Belgrade" },
	{ "W. Central Africa Standard Time", "Africa/Luanda" },
	{ "Jordan Standard Time", "Asia/Amman" },
	{ "GTB Standard Time", "Europe/Athens" },
	{ "Middle East Standard Time", "Asia/Beirut" },
	{ "Egypt Standard Time", "Africa/Cairo" },
	{ "South Africa Standard Time", "Africa/Harare" },
	{ "FLE Standard Time", "Europe/Helsinki" },
	{ "Israel Standard Time", "Asia/Jerusalem" },
	{ "E. Europe Standard Time", "Europe/Minsk" },
	{ "Namibia Standard Time", "Africa/Windhoek" },
	{ "Arabic Standard Time", "Asia/Baghdad" },
	{ "Arab Standard Time", "Asia/Qatar" },
	{ "Russian Standard Time", "Europe/Moscow" },
	{ "E. Africa Standard Time", "Africa/Nairobi" },
	{ "Georgian Standard Time", "Asia/Tbilisi" },
	{ "Iran Standard Time", "Asia/Tehran" },
	{ "Arabian Standard Time", "Asia/Muscat" },
	{ "Azerbaijan Standard Time", "Asia/Baku" },
	{ "Caucasus Standard Time", "Asia/Yerevan" },
	{ "Armenian Standard Time", "Asia/Yerevan" },
	{ "Afghanistan Standard Time", "Asia/Kabul" },
	{ "Ekaterinburg Standard Time", "Asia/Yekaterinburg" },
	{ "West Asia Standard Time", "Asia/Karachi" },
	{ "India Standard Time", "Asia/Kolkata" },
	{ "Sri Lanka Standard Time", "Asia/Colombo" },
	{ "Nepal Standard Time", "Asia/Kathmandu" },
	{ "N. Central Asia Standard Time", "Asia/Novosibirsk" },
	{ "Central Asia Standard Time", "Asia/Dhaka" },
	{ "Myanmar Standard Time", "Asia/Rangoon" },
	{ "SE Asia Standard Time", "Asia/Bangkok" },
	{ "North Asia Standard Time", "Asia/Krasnoyarsk" },
	{ "China Standard Time", "Asia/Shanghai" },
	{ "North Asia East Standard Time", "Asia/Ulaanbaatar" },
	{ "Singapore Standard Time", "Asia/Singapore" },
	{ "W. Australia Standard Time", "Australia/Perth" },
	{ "Taipei Standard Time", "Asia/Taipei" },
	{ "Tokyo Standard Time", "Asia/Tokyo" },
	{ "Korea Standard Time", "Asia/Seoul" },
	{ "Yakutsk Standard Time", "Asia/Yakutsk" },
	{ "Cen. Australia Standard Time", "Australia/Adelaide" },
	{ "AUS Central Standard Time", "Australia/Darwin" },
	{ "E. Australia Standard Time", "Australia/Brisbane" },
	{ "AUS Eastern Standard Time", "Australia/Sydney" },
	{ "West Pacific Standard Time", "Pacific/Guam" },
	{ "Tasmania Standard Time", "Australia/Hobart" },
	{ "Vladivostok Standard Time", "Asia/Vladivostok" },
	{ "Central Pacific Standard Time", "Asia/Magadan" },
	{ "New Zealand Standard Time", "Pacific/Auckland" },
	{ "Fiji Standard Time", "Pacific/Fiji" },
	{ "Tonga Standard Time", "Pacific/Tongatapu" },
};

static const struct tz_map ical_to_msdn_table[] = {
	{ "UTC", "UTC" },
	{ "Africa/Abidjan", "Greenwich Standard Time" },
	{ "Africa/Accra", "Greenwich Standard Time" },
	{ "Africa/Addis_Ababa", "E. Africa Standard Time" },
	{ "Africa/Algiers", "W. Central Africa Standard Time" },
	{ "Africa/Asmara", "E. Africa Standard Time" },
	{ "Africa/Bamako", "Greenwich Standard Time" },
	{ "Africa/Bangui", "W. Central Africa Standard Time" },
	{ "Africa/Banjul", "Greenwich Standard Time" },
	{ "Africa/Bissau", "Greenwich Standard Time" },
	{ "Africa/Blantyre", "South Africa Standard Time" },
	{ "Africa/Brazzaville", "W. Central Africa Standard Time" },
	{ "Africa/Bujumbura", "South Africa Standard Time" },
	{ "Africa/Cairo", "Egypt Standard Time" },
	{ "Africa/Casablanca", "Greenwich Standard Time" },
	{ "Africa/Ceuta", "W. Central Africa Standard Time" },
	{ "Africa/Conakry", "Greenwich Standard Time" },
	{ "Africa/Dakar", "Greenwich Standard Time" },
	{ "Africa/Dar_es_Salaam", "E. Africa Standard Time" },
	{ "Africa/Djibouti", "E. Africa Standard Time" },
	{ "Africa/Douala", "W. Central Africa Standard Time" },
	{ "Africa/El_Aaiun", "Greenwich Standard Time" },
	{ "Africa/Freetown", "Greenwich Standard Time" },
	{ "Africa/Gaborone", "South Africa Standard Time" },
	{ "Africa/Harare", "South Africa Standard Time" },
	{ "Africa/Johannesburg", "South Africa Standard Time" },
	{ "Africa/Juba", "E. Africa Standard Time" },
	{ "Africa/Kampala", "E. Africa Standard Time" },
	{ "Africa/Khartoum", "E. Africa Standard Time" },
	{ "Africa/Kigali", "Egypt Standard Time" },
	{ "Africa/Kinshasa", "W. Central Africa Standard Time" },
	{ "Africa/Lagos", "Greenwich Standard Time" },
	{ "Africa/Libreville", "W. Central Africa Standard Time" },
	{ "Africa/Lome", "Greenwich Standard Time" },
	{ "Africa/Luanda", "W. Central Africa Standard Time" },
	{ "Africa/Lubumbashi", "South Africa Standard Time" },
	{ "Africa/Lusaka", "South Africa Standard Time" },
	{ "Africa/Malabo", "W. Central Africa Standard Time" },
	{ "Africa/Maputo", "South Africa Standard Time" },
	{ "Africa/Maseru", "South Africa Standard Time" },
	{ "Africa/Mbabane", "South Africa Standard Time" },
	{ "Africa/Mogadishu", "E. Africa Standard Time" },
	{ "Africa/Monrovia", "Greenwich Standard Time" },
	{ "Africa/Nairobi", "E. Africa Standard Time" },
	{ "Africa/Ndjamena", "W. Central Africa Standard Time" },
	{ "Africa/Niamey", "W. Central Africa Standard Time" },
	{ "Africa/Nouakchott", "Greenwich Standard Time" },
	{ "Africa/Ouagadougou", "Greenwich Standard Time" },
	{ "Africa/Porto-Novo", "W. Central Africa Standard Time" },
	{ "Africa/Sao_Tome", "Cape Verde Standard Time" },
	{ "Africa/Tripoli", "Egypt Standard Time" },
	{ "Africa/Tunis", "W. Central Africa Standard Time" },
	{ "Africa/Windhoek", "Namibia Standard Time" },
	{ "America/Adak", "Hawaiian Standard Time" },
	{ "America/Anchorage", "Alaskan Standard Time" },
	{ "America/Anguilla", "SA Western Standard Time" },
	{ "America/Antigua", "SA Western Standard Time" },
	{ "America/Araguaina", "E. South America Standard Time" },
	{ "America/Argentina/Buenos_Aires", "SA Eastern Standard Time" },
	{ "America/Argentina/Catamarca", "SA Eastern Standard Time" },
	{ "America/Argentina/Cordoba", "SA Eastern Standard Time" },
	{ "America/Argentina/Jujuy", "SA Eastern Standard Time" },
	{ "America/Argentina/La_Rioja", "SA Eastern Standard Time" },
	{ "America/Argentina/Mendoza", "SA Eastern Standard Time" },
	{ "America/Argentina/Rio_Gallegos", "SA Eastern Standard Time" },
	{ "America/Argentina/Salta", "SA Eastern Standard Time" },
	{ "America/Argentina/San_Luis", "SA Eastern Standard Time" },
	{ "America/Argentina/San_Juan", "SA Eastern Standard Time" },
	{ "America/Argentina/Tucuman", "SA Eastern Standard Time" },
	{ "America/Argentina/Ushuaia", "SA Eastern Standard Time" },
	{ "America/Aruba", "Venezuela Standard Time" },
	{ "America/Asuncion", "SA Eastern Standard Time" },
	{ "America/Atikokan", "US Eastern Standard Time" },
	{ "America/Bahia", "E. South America Standard Time" },
	{ "America/Bahia_Banderas", "Central Standard Time (Mexico)" },
	{ "America/Barbados", "SA Western Standard Time" },
	{ "America/Belem", "E. South America Standard Time" },
	{ "America/Belize", "Mexico Standard Time" },
	{ "America/Blanc-Sablon", "Atlantic Standard Time" },
	{ "America/Boa_Vista", "SA Western Standard Time" },
	{ "America/Bogota", "SA Pacific Standard Time" },
	{ "America/Boise", "US Mountain Standard Time" },
	{ "America/Cambridge_Bay", "Mountain Standard Time" },
	{ "America/Campo_Grande", "E. South America Standard Time" },
	{ "America/Cancun", "Central America Standard Time" },
	{ "America/Caracas", "Venezuela Standard Time" },
	{ "America/Cayenne", "E. South America Standard Time" },
	{ "America/Cayman", "SA Pacific Standard Time" },
	{ "America/Chicago", "Central Standard Time" },
	{ "America/Chihuahua", "Mexico Standard Time 2" },
	{ "America/Costa_Rica", "Central America Standard Time" },
	{ "America/Creston", "Mountain Standard Time" },
	{ "America/Cuiaba", "E. South America Standard Time" },
	{ "America/Curacao", "Venezuela Standard Time" },
	{ "America/Danmarkshavn", "GMT Standard Time" },
	{ "America/Dawson", "Pacific Standard Time" },
	{ "America/Dawson_Creek", "Mountain Standard Time" },
	{ "America/Denver", "Mountain Standard Time" },
	{ "America/Detroit", "Eastern Standard Time" },
	{ "America/Dominica", "SA Western Standard Time" },
	{ "America/Edmonton", "Mountain Standard Time" },
	{ "America/Eirunepe", "SA Pacific Standard Time" },
	{ "America/El_Salvador", "Central America Standard Time" },
	{ "America/Fortaleza", "E. South America Standard Time" },
	{ "America/Glace_Bay", "Atlantic Standard Time" },
	{ "America/Godthab", "Greenland Standard Time" },
	{ "America/Goose_Bay", "Atlantic Standard Time" },
	{ "America/Grand_Turk", "SA Pacific Standard Time" },
	{ "America/Grenada", "SA Western Standard Time" },
	{ "America/Guadeloupe", "SA Western Standard Time" },
	{ "America/Guatemala", "Central America Standard Time" },
	{ "America/Guayaquil", "SA Pacific Standard Time" },
	{ "America/Guyana", "Venezuela Standard Time" },
	{ "America/Halifax", "Atlantic Standard Time" },
	{ "America/Havana", "SA Pacific Standard Time" },
	{ "America/Hermosillo", "Mexico Standard Time 2" },
	{ "America/Indiana/Indianapolis", "US Eastern Standard Time" },
	{ "America/Indiana/Knox", "Canada Central Standard Time" },
	{ "America/Indiana/Marengo", "US Eastern Standard Time" },
	{ "America/Indiana/Petersburg", "US Eastern Standard Time" },
	{ "America/Indiana/Tell_City", "Canada Central Standard Time" },
	{ "America/Indiana/Vevay", "US Eastern Standard Time" },
	{ "America/Indiana/Vincennes", "US Eastern Standard Time" },
	{ "America/Indiana/Winamac", "US Eastern Standard Time" },
	{ "America/Inuvik", "Mountain Standard Time" },
	{ "America/Iqaluit", "Eastern Standard Time" },
	{ "America/Jamaica", "SA Pacific Standard Time" },
	{ "America/Juneau", "Alaskan Standard Time" },
	{ "America/Kentucky/Louisville", "US Eastern Standard Time" },
	{ "America/Kentucky/Monticello", "US Eastern Standard Time" },
	{ "America/Kralendijk", "Venezuela Standard Time" },
	{ "America/La_Paz", "SA Western Standard Time" },
	{ "America/Lima", "SA Pacific Standard Time" },
	{ "America/Los_Angeles", "Pacific Standard Time" },
	{ "America/Lower_Princes", "Venezuela Standard Time" },
	{ "America/Maceio", "E. South America Standard Time" },
	{ "America/Managua", "Central America Standard Time" },
	{ "America/Manaus", "Central Brazilian Standard Time" },
	{ "America/Marigot", "SA Western Standard Time" },
	{ "America/Martinique", "SA Western Standard Time" },
	{ "America/Matamoros", "Central Standard Time" },
	{ "America/Mazatlan", "Mountain Standard Time (Mexico)" },
	{ "America/Menominee", "Central Standard Time" },
	{ "America/Merida", "Central America Standard Time" },
	{ "America/Metlakatla", "Alaskan Standard Time" },
	{ "America/Mexico_City", "Mexico Standard Time" },
	{ "America/Miquelon", "Greenland Standard Time" },
	{ "America/Moncton", "Atlantic Standard Time" },
	{ "America/Monterrey", "Central Standard Time (Mexico)" },
	{ "America/Montevideo", "Montevideo Standard Time" },
	{ "America/Montreal", "Eastern Standard Time" },
	{ "America/Montserrat", "SA Western Standard Time" },
	{ "America/Nassau", "Eastern Standard Time" },
	{ "America/New_York", "Eastern Standard Time" },
	{ "America/Nipigon", "Eastern Standard Time" },
	{ "America/Nome", "Alaskan Standard Time" },
	{ "America/Noronha", "Mid-Atlantic Standard Time" },
	{ "America/North_Dakota/Beulah", "Central Standard Time" },
	{ "America/North_Dakota/Center", "Central Standard Time" },
	{ "America/North_Dakota/New_Salem", "Central Standard Time" },
	{ "America/Ojinaga", "Mexico Standard Time 2" },
	{ "America/Panama", "SA Pacific Standard Time" },
	{ "America/Pangnirtung", "Eastern Standard Time" },
	{ "America/Paramaribo", "Venezuela Standard Time" },
	{ "America/Phoenix", "US Mountain Standard Time" },
	{ "America/Port-au-Prince", "SA Western Standard Time" },
	{ "America/Port_of_Spain", "SA Western Standard Time" },
	{ "America/Porto_Velho", "SA Western Standard Time" },
	{ "America/Puerto_Rico", "SA Western Standard Time" },
	{ "America/Rainy_River", "Canada Central Standard Time" },
	{ "America/Rankin_Inlet", "Canada Central Standard Time" },
	{ "America/Recife", "E. South America Standard Time" },
	{ "America/Regina", "Central America Standard Time" },
	{ "America/Resolute", "Eastern Standard Time" },
	{ "America/Rio_Branco", "SA Pacific Standard Time" },
	{ "America/Santa_Isabel", "Pacific Standard Time (Mexico)" },
	{ "America/Santarem", "E. South America Standard Time" },
	{ "America/Santiago", "Pacific SA Standard Time" },
	{ "America/Santo_Domingo", "SA Western Standard Time" },
	{ "America/Sao_Paulo", "Mid-Atlantic Standard Time" },
	{ "America/Scoresbysund", "Azores Standard Time" },
	{ "America/Shiprock", "US Mountain Standard Time" },
	{ "America/Sitka", "Alaskan Standard Time" },
	{ "America/St_Barthelemy", "SA Western Standard Time" },
	{ "America/St_Johns", "Newfoundland Standard Time" },
	{ "America/St_Kitts", "SA Western Standard Time" },
	{ "America/St_Lucia", "SA Western Standard Time" },
	{ "America/St_Thomas", "SA Western Standard Time" },
	{ "America/St_Vincent", "SA Western Standard Time" },
	{ "America/Swift_Current", "Central Standard Time" },
	{ "America/Tegucigalpa", "Central America Standard Time" },
	{ "America/Thule", "Atlantic Standard Time" },
	{ "America/Thunder_Bay", "Eastern Standard Time" },
	{ "America/Tijuana", "Pacific Standard Time (Mexico)" },
	{ "America/Toronto", "Eastern Standard Time" },
	{ "America/Tortola", "SA Western Standard Time" },
	{ "America/Vancouver", "Pacific Standard Time" },
	{ "America/Whitehorse", "Pacific Standard Time" },
	{ "America/Winnipeg", "Canada Central Standard Time" },
	{ "America/Yakutat", "Alaskan Standard Time" },
	{ "America/Yellowknife", "Mountain Standard Time" },
	{ "Antarctica/Casey", "GMT Standard Time" },
	{ "Antarctica/Davis", "SE Asia Standard Time" },
	{ "Antarctica/DumontDUrville", "West Pacific Standard Time" },
	{ "Antarctica/Macquarie", "Central Pacific Standard Time" },
	{ "Antarctica/Mawson", "GMT Standard Time" },
	{ "Antarctica/McMurdo", "Tonga Standard Time" },
	{ "Antarctica/Palmer", "Greenland Standard Time" },
	{ "Antarctica/Rothera", "GMT Standard Time" },
	{ "Antarctica/South_Pole", "GMT Standard Time" },
	{ "Antarctica/Syowa", "GMT Standard Time" },
	{ "Antarctica/Vostok", "GMT Standard Time" },
	{ "Arctic/Longyearbyen", "Central Europe Standard Time" },
	{ "Asia/Aden", "Arab Standard Time" },
	{ "Asia/Almaty", "N. Central Asia Standard Time" },
	{ "Asia/Amman", "Jordan Standard Time" },
	{ "Asia/Anadyr", "Fiji Standard Time" },
	{ "Asia/Aqtau", "Ekaterinburg Standard Time" },
	{ "Asia/Aqtobe", "Ekaterinburg Standard Time" },
	{ "Asia/Ashgabat", "Ekaterinburg Standard Time" },
	{ "Asia/Baghdad", "Arabic Standard Time" },
	{ "Asia/Bahrain", "Arab Standard Time" },
	{ "Asia/Baku", "Azerbaijan Standard Time" },
	{ "Asia/Bangkok", "SE Asia Standard Time" },
	{ "Asia/Beijing", "China Standard Time" },
	{ "Asia/Beirut", "Middle East Standard Time" },
	{ "Asia/Bishkek", "Central Asia Standard Time" },
	{ "Asia/Brunei", "Taipei Standard Time" },
	{ "Asia/Kolkata", "India Standard Time" },
	{ "Asia/Choibalsan", "Yakutsk Standard Time" },
	{ "Asia/Chongqing", "China Standard Time" },
	{ "Asia/Colombo", "India Standard Time" },
	{ "Asia/Damascus", "Israel Standard Time" },
	{ "Asia/Dhaka", "Central Asia Standard Time" },
	{ "Asia/Dili", "Yakutsk Standard Time" },
	{ "Asia/Dubai", "Iran Standard Time" },
	{ "Asia/Dushanbe", "West Asia Standard Time" },
	{ "Asia/Gaza", "Israel Standard Time" },
	{ "Asia/Harbin", "China Standard Time" },
	{ "Asia/Hebron", "Israel Standard Time" },
	{ "Asia/Ho_Chi_Minh", "North Asia Standard Time" },
	{ "Asia/Hong_Kong", "China Standard Time" },
	{ "Asia/Hovd", "North Asia Standard Time" },
	{ "Asia/Irkutsk", "North Asia East Standard Time" },
	{ "Asia/Jakarta", "SE Asia Standard Time" },
	{ "Asia/Jayapura", "Yakutsk Standard Time" },
	{ "Asia/Jerusalem", "Israel Standard Time" },
	{ "Asia/Kabul", "Afghanistan Standard Time" },
	{ "Asia/Kamchatka", "Fiji Standard Time" },
	{ "Asia/Karachi", "West Asia Standard Time" },
	{ "Asia/Kashgar", "China Standard Time" },
	{ "Asia/Kathmandu", "Nepal Standard Time" },
	{ "Asia/Khandyga", "Yakutsk Standard Time" },
	{ "Asia/Krasnoyarsk", "North Asia Standard Time" },
	{ "Asia/Kuala_Lumpur", "Singapore Standard Time" },
	{ "Asia/Kuching", "Taipei Standard Time" },
	{ "Asia/Kuwait", "Arab Standard Time" },
	{ "Asia/Macau", "China Standard Time" },
	{ "Asia/Magadan", "Central Pacific Standard Time" },
	{ "Asia/Makassar", "Taipei Standard Time" },
	{ "Asia/Manila", "Taipei Standard Time" },
	{ "Asia/Muscat", "Arabian Standard Time" },
	{ "Asia/Nicosia", "Israel Standard Time" },
	{ "Asia/Novokuznetsk", "N. Central Asia Standard Time" },
	{ "Asia/Novosibirsk", "N. Central Asia Standard Time" },
	{ "Asia/Omsk", "N. Central Asia Standard Time" },
	{ "Asia/Oral", "Ekaterinburg Standard Time" },
	{ "Asia/Phnom_Penh", "SE Asia Standard Time" },
	{ "Asia/Pontianak", "SE Asia Standard Time" },
	{ "Asia/Pyongyang", "Korea Standard Time" },
	{ "Asia/Qatar", "Arab Standard Time" },
	{ "Asia/Qyzylorda", "Central Asia Standard Time" },
	{ "Asia/Rangoon", "Myanmar Standard Time" },
	{ "Asia/Riyadh", "Arab Standard Time" },
	{ "Asia/Saigon", "SE Asia Standard Time" },
	{ "Asia/Sakhalin", "Vladivostok Standard Time" },
	{ "Asia/Samarkand", "West Asia Standard Time" },
	{ "Asia/Seoul", "Korea Standard Time" },
	{ "Asia/Shanghai", "China Standard Time" },
	{ "Asia/Singapore", "Singapore Standard Time" },
	{ "Asia/Taipei", "Taipei Standard Time" },
	{ "Asia/Tashkent", "West Asia Standard Time" },
	{ "Asia/Tbilisi", "Georgian Standard Time" },
	{ "Asia/Tehran", "Iran Standard Time" },
	{ "Asia/Thimphu", "Central Asia Standard Time" },
	{ "Asia/Tokyo", "Tokyo Standard Time" },
	{ "Asia/Ulaanbaatar", "North Asia East Standard Time" },
	{ "Asia/Ust-Nera", "Yakutsk Standard Time" },
	{ "Asia/Urumqi", "China Standard Time" },
	{ "Asia/Vientiane", "SE Asia Standard Time" },
	{ "Asia/Vladivostok", "Vladivostok Standard Time" },
	{ "Asia/Yakutsk", "Yakutsk Standard Time" },
	{ "Asia/Yekaterinburg", "Ekaterinburg Standard Time" },
	{ "Asia/Yerevan", "Armenian Standard Time" },
	{ "Atlantic/Azores", "Azores Standard Time" },
	{ "Atlantic/Bermuda", "Atlantic Standard Time" },
	{ "Atlantic/Canary", "GMT Standard Time" },
	{ "Atlantic/Cape_Verde", "Cape Verde Standard Time" },
	{ "Atlantic/Faroe", "GMT Standard Time" },
	{ "Atlantic/Jan_Mayen", "W. Europe Standard Time" },
	{ "Atlantic/Madeira", "GMT Standard Time" },
	{ "Atlantic/Reykjavik", "GMT Standard Time" },
	{ "Atlantic/South_Georgia", "Mid-Atlantic Standard Time" },
	{ "Atlantic/Stanley", "SA Eastern Standard Time" },
	{ "Atlantic/St_Helena", "Greenwich Standard Time" },
	{ "Australia/Adelaide", "Cen. Australia Standard Time" },
	{ "Australia/Brisbane", "E. Australia Standard Time" },
	{ "Australia/Broken_Hill", "Cen. Australia Standard Time" },
	{ "Australia/Currie", "AUS Eastern Standard Time" },
	{ "Australia/Darwin", "AUS Central Standard Time" },
	{ "Australia/Eucla", "AUS Central Standard Time" },
	{ "Australia/Hobart", "Tasmania Standard Time" },
	{ "Australia/Lindeman", "E. Australia Standard Time" },
	{ "Australia/Lord_Howe", "AUS Eastern Standard Time" },
	{ "Australia/Melbourne", "AUS Eastern Standard Time" },
	{ "Australia/Perth", "W. Australia Standard Time" },
	{ "Australia/Sydney", "AUS Eastern Standard Time" },
	{ "Europe/Amsterdam", "W. Europe Standard Time" },
	{ "Europe/Andorra", "W. Europe Standard Time" },
	{ "Europe/Athens", "GTB Standard Time" },
	{ "Europe/Belgrade", "Central European Standard Time" },
	{ "Europe/Berlin", "W. Europe Standard Time" },
	{ "Europe/Bratislava", "Central Europe Standard Time" },
	{ "Europe/Brussels", "Romance Standard Time" },
	{ "Europe/Bucharest", "E. Europe Standard Time" },
	{ "Europe/Budapest", "Central Europe Standard Time" },
	{ "Europe/Busingen", "W. Europe Standard Time" },
	{ "Europe/Chisinau", "FLE Standard Time" },
	{ "Europe/Copenhagen", "Romance Standard Time" },
	{ "Europe/Dublin", "GMT Standard Time" },
	{ "Europe/Gibraltar", "Romance Standard Time" },
	{ "Europe/Guernsey", "GMT Standard Time" },
	{ "Europe/Helsinki", "FLE Standard Time" },
	{ "Europe/Isle_of_Man", "GMT Standard Time" },
	{ "Europe/Istanbul", "GTB Standard Time" },
	{ "Europe/Jersey", "GMT Standard Time" },
	{ "Europe/Kaliningrad", "FLE Standard Time" },
	{ "Europe/Kiev", "FLE Standard Time" },
	{ "Europe/Lisbon", "GMT Standard Time" },
	{ "Europe/Ljubljana", "Central Europe Standard Time" },
	{ "Europe/London", "GMT Standard Time" },
	{ "Europe/Luxembourg", "Romance Standard Time" },
	{ "Europe/Madrid", "Romance Standard Time" },
	{ "Europe/Malta", "W. Europe Standard Time" },
	{ "Europe/Mariehamn", "FLE Standard Time" },
	{ "Europe/Minsk", "E. Europe Standard Time" },
	{ "Europe/Monaco", "W. Europe Standard Time" },
	{ "Europe/Moscow", "Russian Standard Time" },
	{ "Europe/Oslo", "W. Europe Standard Time" },
	{ "Europe/Paris", "Romance Standard Time" },
	{ "Europe/Podgorica", "Central European Standard Time" },
	{ "Europe/Prague", "Central Europe Standard Time" },
	{ "Europe/Riga", "FLE Standard Time" },
	{ "Europe/Rome", "W. Europe Standard Time" },
	{ "Europe/Samara", "Caucasus Standard Time" },
	{ "Europe/San_Marino", "W. Europe Standard Time" },
	{ "Europe/Sarajevo", "Central European Standard Time" },
	{ "Europe/Simferopol", "FLE Standard Time" },
	{ "Europe/Skopje", "Central European Standard Time" },
	{ "Europe/Sofia", "FLE Standard Time" },
	{ "Europe/Stockholm", "W. Europe Standard Time" },
	{ "Europe/Tallinn", "FLE Standard Time" },
	{ "Europe/Tirane", "Central European Standard Time" },
	{ "Europe/Uzhgorod", "FLE Standard Time" },
	{ "Europe/Vaduz", "W. Europe Standard Time" },
	{ "Europe/Vatican", "W. Europe Standard Time" },
	{ "Europe/Vienna", "W. Europe Standard Time" },
	{ "Europe/Vilnius", "FLE Standard Time" },
	{ "Europe/Volgograd", "Russian Standard Time" },
	{ "Europe/Warsaw", "Central European Standard Time" },
	{ "Europe/Zagreb", "Central European Standard Time" },
	{ "Europe/Zaporozhye", "FLE Standard Time" },
	{ "Europe/Zurich", "W. Europe Standard Time" },
	{ "Indian/Antananarivo", "E. Africa Standard Time" },
	{ "Indian/Chagos", "Sri Lanka Standard Time" },
	{ "Indian/Christmas", "SE Asia Standard Time" },
	{ "Indian/Cocos", "Myanmar Standard Time" },
	{ "Indian/Comoro", "E. Africa Standard Time" },
	{ "Indian/Kerguelen", "GMT Standard Time" },
	{ "Indian/Mahe", "Iran Standard Time" },
	{ "Indian/Maldives", "West Asia Standard Time" },
	{ "Indian/Mauritius", "Arabian Standard Time" },
	{ "Indian/Mayotte", "E. Africa Standard Time" },
	{ "Indian/Reunion", "Iran Standard Time" },
	{ "Pacific/Apia", "Dateline Standard Time" },
	{ "Pacific/Auckland", "New Zealand Standard Time" },
	{ "Pacific/Chatham", "Tonga Standard Time" },
	{ "Pacific/Chuuk", "West Pacific Standard Time" },
	{ "Pacific/Easter", "SA Pacific Standard Time" },
	{ "Pacific/Efate", "Central Pacific Standard Time" },
	{ "Pacific/Enderbury", "Tonga Standard Time" },
	{ "Pacific/Fakaofo", "Hawaiian Standard Time" },
	{ "Pacific/Fiji", "Fiji Standard Time" },
	{ "Pacific/Funafuti", "Fiji Standard Time" },
	{ "Pacific/Galapagos", "Mexico Standard Time" },
	{ "Pacific/Gambier", "Alaskan Standard Time" },
	{ "Pacific/Guadalcanal", "Central Pacific Standard Time" },
	{ "Pacific/Guam", "West Pacific Standard Time" },
	{ "Pacific/Honolulu", "Hawaiian Standard Time" },
	{ "Pacific/Johnston", "Hawaiian Standard Time" },
	{ "Pacific/Kiritimati", "Tonga Standard Time" },
	{ "Pacific/Kosrae", "Central Pacific Standard Time" },
	{ "Pacific/Kwajalein", "Fiji Standard Time" },
	{ "Pacific/Majuro", "Central Pacific Standard Time" },
	{ "Pacific/Marquesas", "Alaskan Standard Time" },
	{ "Pacific/Midway", "Samoa Standard Time" },
	{ "Pacific/Nauru", "Fiji Standard Time" },
	{ "Pacific/Niue", "Samoa Standard Time" },
	{ "Pacific/Norfolk", "Central Pacific Standard Time" },
	{ "Pacific/Noumea", "Central Pacific Standard Time" },
	{ "Pacific/Pago_Pago", "Samoa Standard Time" },
	{ "Pacific/Palau", "Yakutsk Standard Time" },
	{ "Pacific/Pitcairn", "Pacific Standard Time" },
	{ "Pacific/Pohnpei", "Central Pacific Standard Time" },
	{ "Pacific/Ponape", "Central Pacific Standard Time" },
	{ "Pacific/Port_Moresby", "West Pacific Standard Time" },
	{ "Pacific/Rarotonga", "Hawaiian Standard Time" },
	{ "Pacific/Saipan", "West Pacific Standard Time" },
	{ "Pacific/Tahiti", "Hawaiian Standard Time" },
	{ "Pacific/Tarawa", "Fiji Standard Time" },
	{ "Pacific/Tongatapu", "Tonga Standard Time" },
	{ "Pacific/Truk", "West Pacific Standard Time" },
	{ "Pacific/Wake", "Fiji Standard Time" },
	{ "Pacific/Wallis", "Fiji Standard Time" },
};

const gchar *
e_cal_backend_ews_tz_util_get_msdn_equivalent (const gchar *ical_tz_location)
{
	const gchar *msdn_tz_location = NULL;

	g_return_val_if_fail (ical_tz_location != NULL, NULL);

	g_rec_mutex_lock (&tz_mutex);
	msdn_tz_location = g_hash_table_lookup (ical_to_msdn, ical_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return msdn_tz_location;
}

void
e_cal_backend_ews_populate_tz_ical_to_msdn (void)
{
	gint i;

	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL) {
		ical_to_msdn = g_hash_table_ref (ical_to_msdn);
		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	ical_to_msdn = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < G_N_ELEMENTS (ical_to_msdn_table); i++)
		g_hash_table_insert (
			ical_to_msdn,
			(gchar *) ical_to_msdn_table[i].from,
			(gchar *) ical_to_msdn_table[i].to);

	g_rec_mutex_unlock (&tz_mutex);
}

const gchar *
e_cal_backend_ews_tz_util_get_ical_equivalent (const gchar *msdn_tz_location)
{
	const gchar *ical_tz_location = NULL;

	g_return_val_if_fail (msdn_tz_location != NULL, NULL);

	g_rec_mutex_lock (&tz_mutex);
	ical_tz_location = g_hash_table_lookup (msdn_to_ical, msdn_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return ical_tz_location;
}

void
e_cal_backend_ews_populate_tz_msdn_to_ical (void)
{
	gint i;

	g_rec_mutex_lock (&tz_mutex);
	if (msdn_to_ical != NULL) {
		msdn_to_ical = g_hash_table_ref (msdn_to_ical);
		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	msdn_to_ical = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < G_N_ELEMENTS (msdn_to_ical_table); i++)
		g_hash_table_insert (
			msdn_to_ical,
			(gchar *) msdn_to_ical_table[i].from,
			(gchar *) msdn_to_ical_table[i].to);

	g_rec_mutex_unlock (&tz_mutex);
}

void
e_cal_backend_ews_unref_tz_ical_to_msdn (void)
{
	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL)
		g_hash_table_unref (ical_to_msdn);
	g_rec_mutex_unlock (&tz_mutex);
}

void
e_cal_backend_ews_unref_tz_msdn_to_ical (void)
{
	g_rec_mutex_lock (&tz_mutex);
	if (msdn_to_ical != NULL)
		g_hash_table_unref (msdn_to_ical);
	g_rec_mutex_unlock (&tz_mutex);
}

EwsCalendarConvertData *
ews_calendar_convert_data_new (void)
{
	return g_new0 (EwsCalendarConvertData, 1);
}

void
ews_calendar_convert_data_free (EwsCalendarConvertData *convert_data)
{
	if (convert_data != NULL) {
		if (convert_data->connection != NULL)
			g_clear_object (&convert_data->connection);
		if (convert_data->comp != NULL)
			g_clear_object (&convert_data->comp);
		if (convert_data->old_comp != NULL)
			g_clear_object (&convert_data->old_comp);
		if (convert_data->default_zone != NULL)
			icaltimezone_free (convert_data->default_zone, TRUE);
		if (convert_data->icalcomp != NULL)
			icalcomponent_free (convert_data->icalcomp);

		g_free (convert_data->item_id);
		g_free (convert_data->change_key);
		g_free (convert_data->user_email);
		g_free (convert_data->response_type);
		g_slist_free_full (convert_data->users, g_free);

		g_free (convert_data);
	}
}

/*
 * Iterate over the icalcomponent properties and collect attendees
 */
void
e_ews_collect_attendees (icalcomponent *comp,
                         GSList **required,
                         GSList **optional,
                         GSList **resource)
{
	icalproperty *prop;
	icalparameter *param;
	const gchar *str = NULL;
	const gchar *org_email_address = NULL;

	/* we need to know who the orgenizer is so we wont duplicate him/her */
	org_email_address = e_ews_collect_organizer (comp);

	/* iterate over every attendee property */
	for (prop = icalcomponent_get_first_property (comp, ICAL_ATTENDEE_PROPERTY);
		prop != NULL;
		prop = icalcomponent_get_next_property (comp, ICAL_ATTENDEE_PROPERTY)) {

		str = icalproperty_get_attendee (prop);

		if (!str || !*str)
			continue;

		/* figure the email address of the attendee, discard "mailto:" if it's there */
		if (!g_ascii_strncasecmp (str, "mailto:", 7))
			str = (str) + 7;

		if (!*str)
			continue;

		/* if this attenddee is the orgenizer - dont add him/her
		 in some cases there is no maito for email if meeting orginazer */
		if (g_ascii_strcasecmp (org_email_address, str) == 0) continue;

		/* figure type of attendee, add to relevant list */
		param = icalproperty_get_first_parameter (prop, ICAL_ROLE_PARAMETER);

		/*in case of new time proposal the role parameter is not a part of ical*/
		if (!param) continue;

		switch (icalparameter_get_role (param)) {
		case ICAL_ROLE_OPTPARTICIPANT:
			*optional = g_slist_append (*optional, (gpointer)str);
			break;
		case ICAL_ROLE_CHAIR:
		case ICAL_ROLE_REQPARTICIPANT:
			*required = g_slist_append (*required, (gpointer)str);
			break;
		case ICAL_ROLE_NONPARTICIPANT:
			*resource = g_slist_append (*resource, (gpointer)str);
			break;
		case ICAL_ROLE_X:
		case ICAL_ROLE_NONE:
			/* Ignore these for now */
			break;
		}
	}

	if (*required == NULL && *optional == NULL && *resource == NULL && org_email_address != NULL)
		*required = g_slist_prepend (*required, (gpointer) org_email_address);
}

gint
ews_get_alarm (ECalComponent *comp)
{
	GList *alarm_uids = e_cal_component_get_alarm_uids (comp);
	ECalComponentAlarm *alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));
	ECalComponentAlarmAction action;
	ECalComponentAlarmTrigger trigger;
	gint dur_int = 0;

	e_cal_component_alarm_get_action (alarm, &action);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		switch (trigger.type) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
			dur_int = ((icaldurationtype_as_int (trigger.u.rel_duration)) / SECS_IN_MINUTE) * -1;
			break;
		default:
			break;
		}
	}
	e_cal_component_alarm_free (alarm);
	cal_obj_uid_list_free (alarm_uids);
	return dur_int;
}

void
ews_set_alarm (ESoapMessage *msg,
               ECalComponent *comp)
{
	/* We know there would be only a single alarm in EWS calendar item */
	GList *alarm_uids = e_cal_component_get_alarm_uids (comp);
	ECalComponentAlarm *alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));
	ECalComponentAlarmAction action;

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "true");
	e_cal_component_alarm_get_action (alarm, &action);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		ECalComponentAlarmTrigger trigger;
		gchar buf[20];
		gint dur_int = 0;
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		switch (trigger.type) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
			dur_int = ((icaldurationtype_as_int (trigger.u.rel_duration)) / SECS_IN_MINUTE) * -1;
			snprintf (buf, 20, "%d", dur_int);
			e_ews_message_write_string_parameter (msg, "ReminderMinutesBeforeStart", NULL, buf);
			break;
		default:
			break;
		}
	}
	e_cal_component_alarm_free (alarm);
	cal_obj_uid_list_free (alarm_uids);

}

void
ewscal_set_time (ESoapMessage *msg,
                 const gchar *name,
                 icaltimetype *t,
                 gboolean with_timezone)
{
	gchar *str;
	gchar *tz_ident = NULL;

	if (with_timezone) {
		if (t->is_utc || !t->zone || t->zone == icaltimezone_get_utc_timezone ()) {
			tz_ident = g_strdup ("Z");
		} else {
			gint offset, is_daylight, hrs, mins;

			offset = icaltimezone_get_utc_offset (
				icaltimezone_get_utc_timezone (), t, &is_daylight);

			offset = offset * (-1);
			hrs = offset / 60;
			mins = offset % 60;

			if (hrs < 0)
				hrs *= -1;
			if (mins < 0)
				mins *= -1;

			tz_ident = g_strdup_printf ("%s%02d:%02d", offset > 0 ? "+" : "-", hrs, mins);
		}
	}

	str = g_strdup_printf (
		"%04d-%02d-%02dT%02d:%02d:%02d%s",
		t->year, t->month, t->day,
		t->hour, t->minute, t->second,
		tz_ident ? tz_ident : "");

	e_ews_message_write_string_parameter (msg, name, NULL, str);

	g_free (tz_ident);
	g_free (str);
}

static void
ewscal_set_date (ESoapMessage *msg,
                 const gchar *name,
                 icaltimetype *t)
{
	gchar *str;

	str = g_strdup_printf (
		"%04d-%02d-%02d",
		t->year, t->month, t->day);

	e_ews_message_write_string_parameter (msg, name, NULL, str);
	g_free (str);
}

static const gchar *number_to_month (gint num) {
	static const gchar *months[] = {
		"January", "February", "March", "April", "May", "June", "July",
		"August", "September", "October", "November", "December"
	};

	return months[num - 1];
}

static const gchar *number_to_weekday (gint num) {
	static const gchar *days[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday",
		"Day", "Weekday", "WeekendDay"
	};

	return days[num - 1];
}

static const gchar *weekindex_to_ical (gint index) {
	static struct {
		const gchar *exch;
		gint index;
	} table[] = {
		{ "First", 1 },
		{ "Second", 2 },
		{ "Third", 3 },
		{ "Fourth", 4 },
		{ "Fifth", 5 },
		{ "Last", -1 }
	};
	gint i;

	for (i = 0; i < 6; i++) {
		if (index == table[i].index)
				return table[i].exch;
	}

	return 0;
}

static void
ewscal_add_rrule (ESoapMessage *msg,
                  icalproperty *prop)
{
	struct icalrecurrencetype recur = icalproperty_get_rrule (prop);

	e_soap_message_start_element (msg, "RelativeYearlyRecurrence", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
	e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical (icalrecurrencetype_day_position (recur.by_day[0])));
	e_ews_message_write_string_parameter (msg, "Month", NULL, number_to_month (recur.by_month[0]));

	e_soap_message_end_element (msg); /* "RelativeYearlyRecurrence" */
}

static void
ewscal_add_timechange (ESoapMessage *msg,
                       icalcomponent *comp,
                       gint baseoffs)
{
	gchar buffer[16], *offset;
	const gchar *tzname;
	icalproperty *prop;
	struct icaltimetype dtstart;
	gint utcoffs;

	prop = icalcomponent_get_first_property (comp, ICAL_TZNAME_PROPERTY);
	if (prop) {
		tzname = icalproperty_get_tzname (prop);
		e_soap_message_add_attribute (msg, "TimeZoneName", tzname, NULL, NULL);
	}

	/* Calculate zone Offset from BaseOffset */
	prop = icalcomponent_get_first_property (comp, ICAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		utcoffs = -icalproperty_get_tzoffsetto (prop);
		utcoffs -= baseoffs;
		offset = icaldurationtype_as_ical_string_r (icaldurationtype_from_int (utcoffs));
		e_ews_message_write_string_parameter (msg, "Offset", NULL, offset);
		free (offset);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	if (prop)
		ewscal_add_rrule (msg, prop);

	prop = icalcomponent_get_first_property (comp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = icalproperty_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
	}
}

static void
ewscal_set_absolute_date_transitions (ESoapMessage *msg,
				      GSList *absolute_date_transitions)
{
	GSList *l;

	if (absolute_date_transitions == NULL)
		return;

	for (l = absolute_date_transitions; l != NULL; l = l->next) {
		EEwsCalendarAbsoluteDateTransition *adt = l->data;

		e_soap_message_start_element (msg, "AbsoluteDateTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, adt->to->value,
			"Kind", adt->to->kind);
		e_ews_message_write_string_parameter (msg, "DateTime", NULL, adt->date_time);

		e_soap_message_end_element (msg); /* "AbsoluteDateTransition" */
	}
}

static void
ewscal_set_recurring_day_transitions (ESoapMessage *msg,
				      GSList *recurring_day_transitions)
{
	GSList *l;

	if (recurring_day_transitions == NULL)
		return;

	for (l = recurring_day_transitions; l != NULL; l = l->next) {
		EEwsCalendarRecurringDayTransition *rdt = l->data;

		e_soap_message_start_element (msg, "RecurringDayTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, rdt->to->value,
			"Kind", rdt->to->kind);
		e_ews_message_write_string_parameter (msg, "TimeOffset", NULL, rdt->time_offset);
		e_ews_message_write_string_parameter (msg, "Month", NULL, rdt->month);
		e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, rdt->day_of_week);
		e_ews_message_write_string_parameter (msg, "Occurrence", NULL, rdt->occurrence);

		e_soap_message_end_element (msg); /* "RecurringDayTransition" */
	}
}

static void
ewscal_set_recurring_date_transitions (ESoapMessage *msg,
				       GSList *recurring_date_transitions)
{
	GSList *l;

	if (recurring_date_transitions == NULL)
		return;

	for (l = recurring_date_transitions; l != NULL; l = l->next) {
		EEwsCalendarRecurringDateTransition *rdt = l->data;

		e_soap_message_start_element (msg, "RecurringDateTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, rdt->to->value,
			"Kind", rdt->to->kind);
		e_ews_message_write_string_parameter (msg, "TimeOffset", NULL, rdt->time_offset);
		e_ews_message_write_string_parameter (msg, "Month", NULL, rdt->month);
		e_ews_message_write_string_parameter (msg, "Day", NULL, rdt->day);

		e_soap_message_end_element (msg); /* "RecurringDateTransition" */
	}
}

void
ewscal_set_timezone (ESoapMessage *msg,
		     const gchar *name,
		     EEwsCalendarTimeZoneDefinition *tzd)
{
	GSList *l;

	if (name == NULL || tzd == NULL)
		return;

	e_soap_message_start_element (msg, name, NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", tzd->id, NULL, NULL);
	e_soap_message_add_attribute (msg, "Name", tzd->name, NULL, NULL);

	e_soap_message_start_element (msg, "Periods", NULL, NULL);
	for (l = tzd->periods; l != NULL; l = l->next) {
		EEwsCalendarPeriod *period = l->data;

		e_soap_message_start_element (msg, "Period", NULL, NULL);
		e_soap_message_add_attribute (msg, "Bias", period->bias, NULL, NULL);
		e_soap_message_add_attribute (msg, "Name", period->name, NULL, NULL);
		e_soap_message_add_attribute (msg, "Id", period->id, NULL, NULL);
		e_soap_message_end_element (msg); /* "Period" */
	}
	e_soap_message_end_element (msg); /* "Periods" */

	e_soap_message_start_element (msg, "TransitionsGroups", NULL, NULL);
	for (l = tzd->transitions_groups; l != NULL; l = l->next) {
		EEwsCalendarTransitionsGroup *tg = l->data;

		e_soap_message_start_element (msg, "TransitionsGroup", NULL, NULL);
		e_soap_message_add_attribute (msg, "Id", tg->id, NULL, NULL);

		if (tg->transition != NULL) {
			e_soap_message_start_element (msg, "Transition", NULL, NULL);
			e_ews_message_write_string_parameter_with_attribute (
				msg,
				"To", NULL, tg->transition->value,
				"Kind", tg->transition->kind);
			e_soap_message_end_element (msg); /* "Transition" */
		}

		ewscal_set_absolute_date_transitions (msg, tg->absolute_date_transitions);
		ewscal_set_recurring_day_transitions (msg, tg->recurring_day_transitions);
		ewscal_set_recurring_date_transitions (msg, tg->recurring_date_transitions);

		e_soap_message_end_element (msg); /* "TransitionsGroup" */
	}
	e_soap_message_end_element (msg); /* "TransitionsGroups" */

	e_soap_message_start_element (msg, "Transitions", NULL, NULL);
	e_soap_message_start_element (msg, "Transition", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (
		msg,
		"To", NULL, tzd->transitions->transition->value,
		"Kind", tzd->transitions->transition->kind);
	e_soap_message_end_element (msg); /* "Transition" */
	ewscal_set_absolute_date_transitions (msg, tzd->transitions->absolute_date_transitions);
	ewscal_set_recurring_day_transitions (msg, tzd->transitions->recurring_day_transitions);
	ewscal_set_recurring_date_transitions (msg, tzd->transitions->recurring_date_transitions);
	e_soap_message_end_element (msg); /* "Transitions" */

	e_soap_message_end_element (msg); /* "StartTimeZone" */
}

void
ewscal_set_meeting_timezone (ESoapMessage *msg,
			     icaltimezone *icaltz)
{
	icalcomponent *comp;
	icalproperty *prop;
	const gchar *location;
	icalcomponent *xstd, *xdaylight;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = icaltimezone_get_component (icaltz);

	/* Exchange needs a BaseOffset, followed by either *both*
	 * Standard and Daylight zones, or neither of them. If there's
	 * more than one STANDARD or DAYLIGHT component in the VTIMEZONE,
	 * we ignore the extra. So fully-specified timezones including
	 * historical DST rules cannot be handled by Exchange. */

	/* FIXME: Walk through them all to find the *latest* ones, like
	 * icaltimezone_get_tznames_from_vtimezone() does. */
	xstd = icalcomponent_get_first_component (comp, ICAL_XSTANDARD_COMPONENT);
	xdaylight = icalcomponent_get_first_component (comp, ICAL_XDAYLIGHT_COMPONENT);

	/* If there was only a DAYLIGHT component, swap them over and pretend
	 * it was the STANDARD component. We're only going to give the server
	 * the BaseOffset anyway. */
	if (!xstd) {
		xstd = xdaylight;
		xdaylight = NULL;
	}

	/* Find a suitable string to use for the TimeZoneName */
	location = icaltimezone_get_location (icaltz);
	if (!location)
		location = icaltimezone_get_tzid (icaltz);
	if (!location)
		location = icaltimezone_get_tznames (icaltz);

	e_soap_message_start_element (msg, "MeetingTimeZone", NULL, NULL);
	e_soap_message_add_attribute (msg, "TimeZoneName", location, NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		prop = icalcomponent_get_first_property (xstd, ICAL_TZOFFSETTO_PROPERTY);
		std_utcoffs = -icalproperty_get_tzoffsetto (prop);
	} else {
		/* UTC has no properties at all, so just set manually */
		std_utcoffs = 0;
	}

	/* This is the overall BaseOffset tag, which the Standard and Daylight
	 * zones are offset from. It's redundant, but Exchange always sets it
	 * to the offset of the Standard zone, and the Offset in the Standard
	 * zone to zero. So try to avoid problems by doing the same. */
	offset = icaldurationtype_as_ical_string_r (icaldurationtype_from_int (std_utcoffs));
	e_ews_message_write_string_parameter (msg, "BaseOffset", NULL, offset);
	free (offset);

	/* Only write the full TimeChangeType information, including the
	 * recurrence rules for the DST changes, if there is more than
	 * one. */
	if (xdaylight) {
		/* Standard */
		e_soap_message_start_element (msg, "Standard", NULL, NULL);
		ewscal_add_timechange (msg, xstd, std_utcoffs);
		e_soap_message_end_element (msg); /* "Standard" */

		/* DayLight */
		e_soap_message_start_element (msg, "Daylight", NULL, NULL);
		ewscal_add_timechange (msg, xdaylight, std_utcoffs);
		e_soap_message_end_element (msg); /* "Daylight" */
	}
	e_soap_message_end_element (msg); /* "MeetingTimeZone" */
}

static void
ewscal_add_availability_rrule (ESoapMessage *msg,
                               icalproperty *prop)
{
	struct icalrecurrencetype recur = icalproperty_get_rrule (prop);
	gchar buffer[16];
	gint dayorder;

	dayorder = icalrecurrencetype_day_position (recur.by_day[0]);
	dayorder = dayorder % 5;
	if (dayorder < 0)
		dayorder += 5;
	dayorder += 1;

	/* expected value is 1..5, inclusive */
	snprintf (buffer, 16, "%d", dayorder);
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, buffer);

	snprintf (buffer, 16, "%d", recur.by_month[0]);
	e_ews_message_write_string_parameter (msg, "Month", NULL, buffer);

	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
}

static void
ewscal_add_availability_default_timechange (ESoapMessage *msg)
{

	e_soap_message_start_element (msg, "StandardTime", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Time", NULL, "00:00:00");
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Month", NULL, "0");
	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, "Sunday");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "DaylightTime", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Time", NULL, "00:00:00");
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Month", NULL, "0");
	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, "Sunday");
	e_soap_message_end_element (msg);
}

static void
ewscal_add_availability_timechange (ESoapMessage *msg,
                                    icalcomponent *comp,
                                    gint baseoffs)
{
	gchar buffer[16];
	icalproperty *prop;
	struct icaltimetype dtstart;
	gint utcoffs;

	/* Calculate zone Offset from BaseOffset */
	prop = icalcomponent_get_first_property (comp, ICAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		utcoffs = -icalproperty_get_tzoffsetto (prop) / 60;
		utcoffs -= baseoffs;
		snprintf (buffer, 16, "%d", utcoffs);
		e_ews_message_write_string_parameter (msg, "Bias", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = icalproperty_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	if (prop)
		ewscal_add_availability_rrule (msg, prop);
}

void
ewscal_set_availability_timezone (ESoapMessage *msg,
                                  icaltimezone *icaltz)
{
	icalcomponent *comp;
	icalproperty *prop;
	icalcomponent *xstd, *xdaylight;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = icaltimezone_get_component (icaltz);

	xstd = icalcomponent_get_first_component (comp, ICAL_XSTANDARD_COMPONENT);
	xdaylight = icalcomponent_get_first_component (comp, ICAL_XDAYLIGHT_COMPONENT);

	/*TimeZone is the root element of GetUserAvailabilityRequest*/
	e_soap_message_start_element (msg, "TimeZone", NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		prop = icalcomponent_get_first_property (xstd, ICAL_TZOFFSETTO_PROPERTY);
		std_utcoffs = -icalproperty_get_tzoffsetto (prop) / 60;
	} else
		std_utcoffs = 0;

	/* This is the overall BaseOffset tag, which the Standard and Daylight
	 * zones are offset from. It's redundant, but Exchange always sets it
	 * to the offset of the Standard zone, and the Offset in the Standard
	 * zone to zero. So try to avoid problems by doing the same. */
	offset = g_strdup_printf ("%d", std_utcoffs);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, offset);
	g_free (offset);

	if (xdaylight) {
		/* Standard */
		e_soap_message_start_element (msg, "StandardTime", NULL, NULL);
		ewscal_add_availability_timechange (msg, xstd, std_utcoffs);
		e_soap_message_end_element (msg); /* "StandardTime" */

		/* DayLight */
		e_soap_message_start_element (msg, "DaylightTime", NULL, NULL);
		ewscal_add_availability_timechange (msg, xdaylight, std_utcoffs);
		e_soap_message_end_element (msg); /* "DaylightTime" */
	} else
		/* Set default values*/
		ewscal_add_availability_default_timechange (msg);

	e_soap_message_end_element (msg); /* "TimeZone" */
}

void
ewscal_set_reccurence (ESoapMessage *msg,
                       icalproperty *rrule,
                       icaltimetype *dtstart)
{
	gchar buffer[256];
	gint i, len;

	/* MSDN reference: http://msdn.microsoft.com/en-us/library/aa580471%28v=EXCHG.80%29.aspx
	 */
	struct icalrecurrencetype recur = icalproperty_get_rrule (rrule);

	e_soap_message_start_element (msg, "Recurrence", NULL, NULL);

	switch (recur.freq) {
		case ICAL_DAILY_RECURRENCE:
			e_soap_message_start_element (msg, "DailyRecurrence", NULL, NULL);
			snprintf (buffer, 32, "%d", recur.interval);
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);
			e_soap_message_end_element (msg); /* "DailyRecurrence" */
			break;

		case ICAL_WEEKLY_RECURRENCE:
			e_soap_message_start_element (msg, "WeeklyRecurrence", NULL, NULL);

			snprintf (buffer, 32, "%d", recur.interval);
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

			len = snprintf (
				buffer, 256, "%s",
				number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
			for (i = 1; recur.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
				len += snprintf (
					buffer + len, 256 - len, " %s",
					number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[i])));
			}
			e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, buffer);

			e_soap_message_end_element (msg); /* "WeeklyRecurrence" */
			break;

		case ICAL_MONTHLY_RECURRENCE:
			if (recur.by_month_day[0] == ICAL_RECURRENCE_ARRAY_MAX) {
				e_soap_message_start_element (msg, "RelativeMonthlyRecurrence", NULL, NULL);

				/* For now this is what got implemented since this is the only
				 relative monthly recurrence evolution can set.
				 TODO: extend the code with all possible monthly recurrence settings */
				snprintf (buffer, 32, "%d", recur.interval);
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				e_ews_message_write_string_parameter (
					msg, "DaysOfWeek", NULL,
					number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));

				e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical ((recur.by_set_pos[0] == 5 ? -1 : recur.by_set_pos[0])));

				e_soap_message_end_element (msg); /* "RelativeMonthlyRecurrence" */

			} else {
				e_soap_message_start_element (msg, "AbsoluteMonthlyRecurrence", NULL, NULL);

				snprintf (buffer, 256, "%d", recur.interval);
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				snprintf (buffer, 256, "%d", recur.by_month_day[0]);
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				e_soap_message_end_element (msg); /* "AbsoluteMonthlyRecurrence" */

			}
			break;

		case ICAL_YEARLY_RECURRENCE:
			#if 0 /* FIXME */
			if (is_relative) {
				ewscal_add_rrule (msg, rrule);

			} else
			#endif
			{
				e_soap_message_start_element (msg, "AbsoluteYearlyRecurrence", NULL, NULL);

				/* work according to RFC5545 §3.3.10
				 * dtstart is the default, give preference to by_month & by_month_day if they are set
				 */
				if (recur.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", recur.by_month_day[0]);
				} else {
					snprintf (buffer, 256, "%d", dtstart->day);
				}
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				if (recur.by_month[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", recur.by_month_day[0]);
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (recur.by_month[0]));
				} else {
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (dtstart->month));
				}

				e_soap_message_end_element (msg); /* "AbsoluteYearlyRecurrence" */

			}
			break;

		case ICAL_SECONDLY_RECURRENCE:
		case ICAL_MINUTELY_RECURRENCE:
		case ICAL_HOURLY_RECURRENCE:
		default:
			/* TODO: remove the "Recurrence" element somehow */
			g_warning ("EWS cant handle recurrence with frequency higher than DAILY\n");
			goto exit;
	}

	if (recur.count > 0) {
		e_soap_message_start_element (msg, "NumberedRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		snprintf (buffer, 32, "%d", recur.count);
		e_ews_message_write_string_parameter (msg, "NumberOfOccurrences", NULL, buffer);
		e_soap_message_end_element (msg); /* "NumberedRecurrence" */

	} else if (!icaltime_is_null_time (recur.until)) {
		e_soap_message_start_element (msg, "EndDateRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		ewscal_set_date (msg, "EndDate", &recur.until);
		e_soap_message_end_element (msg); /* "EndDateRecurrence" */

	} else {
		e_soap_message_start_element (msg, "NoEndRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		e_soap_message_end_element (msg); /* "NoEndRecurrence" */
	}

exit:
	e_soap_message_end_element (msg); /* "Recurrence" */
}

static struct icaltimetype
icalcomponent_get_datetime (icalcomponent *comp,
                            icalproperty *prop)
{
	/* Extract datetime with proper timezone */
	icalcomponent *c;
	icalparameter *param;
	struct icaltimetype ret;

	ret = icalvalue_get_datetime (icalproperty_get_value (prop));

	if ((param = icalproperty_get_first_parameter (prop, ICAL_TZID_PARAMETER)) != NULL) {
		const gchar *tzid = icalparameter_get_tzid (param);
		icaltimezone *tz = NULL;

		for (c = comp; c != NULL; c = icalcomponent_get_parent (c)) {
			tz = icalcomponent_get_timezone (c, tzid);
			if (tz != NULL)	break;
		}

		if (tz == NULL)
			tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);

		if (tz != NULL)
			ret = icaltime_set_timezone (&ret, tz);
	}

	return ret;
}

void
ewscal_set_reccurence_exceptions (ESoapMessage *msg,
                                  icalcomponent *comp)
{
	icalproperty *exdate;

	/* Make sure we have at least 1 excluded occurrence */
	exdate = icalcomponent_get_first_property (comp,ICAL_EXDATE_PROPERTY);
	if (!exdate) return;

	e_soap_message_start_element (msg, "DeletedOccurrences", NULL, NULL);

	for (; exdate; exdate = icalcomponent_get_next_property (comp, ICAL_EXDATE_PROPERTY)) {
		struct icaltimetype exdatetime = icalcomponent_get_datetime (comp, exdate);

		e_soap_message_start_element (msg, "DeletedOccurrence", NULL, NULL);

		ewscal_set_date (msg, "Start", &exdatetime);

		e_soap_message_end_element (msg); /* "DeletedOccurrence" */
	}

	e_soap_message_end_element (msg); /* "DeletedOccurrences" */
}

void
ewscal_get_attach_differences (const GSList *original,
                               const GSList *modified,
                               GSList **removed,
                               GSList **added)
{
	gboolean flag;
	GSList *i, *i_next, *j, *j_next, *original_copy, *modified_copy;
	original_copy = g_slist_copy ((GSList *) original);
	modified_copy = g_slist_copy ((GSList *) modified);

	for (j = modified_copy; j; j = j_next) {
		j_next = j->next;

		for (i = original_copy, flag = FALSE; !flag && i; i = i_next) {
			i_next = i->next;

			if (g_strcmp0 (j->data, i->data) == 0) {
				/* Remove from the lists attachments that are on both */
				original_copy = g_slist_delete_link (original_copy, i);
				modified_copy = g_slist_delete_link (modified_copy, j);
				flag = TRUE;
			}
		}
	}

	*removed = original_copy;
	*added = modified_copy;
}

/*
 * get meeting organizer e-mail address
 */
const gchar *
e_ews_collect_organizer (icalcomponent *comp)
{
	icalproperty *org_prop = NULL;
	const gchar *org = NULL;
	const gchar *org_email_address = NULL;

	org_prop = icalcomponent_get_first_property (comp, ICAL_ORGANIZER_PROPERTY);
	org = icalproperty_get_organizer (org_prop);
	if (!org)
		return NULL;

	if (g_ascii_strncasecmp (org, "MAILTO:", 7) == 0)
		org = org + 7;

	org_email_address = org;

	if (org_email_address && !*org_email_address)
		org_email_address = NULL;

	return org_email_address;
}

gchar *
e_ews_extract_attachment_id_from_uri (const gchar *uri)
{
	gchar *attachment_id, *filepath = g_filename_from_uri (uri, NULL, NULL);
	gchar **dirs = g_strsplit (filepath, "/", 0);
	gint n = 0;

	while (dirs[n]) n++;

	attachment_id = g_strdup (dirs[n - 1]);

	g_strfreev (dirs);

	return attachment_id;
}

void
e_ews_clean_icalcomponent (icalcomponent *icalcomp)
{
	icalproperty *prop, *item_id_prop = NULL, *changekey_prop = NULL;

	prop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name = icalproperty_get_x_name (prop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-ITEMID"))
			item_id_prop = prop;
		 else if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY"))
			changekey_prop = prop;

		prop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	if (item_id_prop != NULL)
		icalcomponent_remove_property (icalcomp, item_id_prop);

	if (changekey_prop != NULL)
		icalcomponent_remove_property (icalcomp, changekey_prop);
}

static void
add_attendees_list_to_message (ESoapMessage *msg,
                               const gchar *listname,
                               GSList *list)
{
	GSList *item;

	e_soap_message_start_element (msg, listname, NULL, NULL);

	for (item = list; item != NULL; item = item->next) {
		e_soap_message_start_element (msg, "Attendee", NULL, NULL);
		e_soap_message_start_element (msg, "Mailbox", NULL, NULL);

		e_ews_message_write_string_parameter (msg, "EmailAddress", NULL, item->data);

		e_soap_message_end_element (msg); /* "Mailbox" */
		e_soap_message_end_element (msg); /* "Attendee" */
	}

	e_soap_message_end_element (msg);
}

static void
convert_sensitivity_calcomp_to_xml (ESoapMessage *msg,
				    icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icalcomp != NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Normal");
		} else if (classify == ICAL_CLASS_PRIVATE) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Private");
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Personal");
		}
	}
}

static void
convert_categories_calcomp_to_xml (ESoapMessage *msg,
				   ECalComponent *comp,
				   icalcomponent *icalcomp)
{
	GSList *categ_list, *citer;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icalcomp != NULL);

	if (comp) {
		g_object_ref (comp);
	} else {
		icalcomponent *clone = icalcomponent_new_clone (icalcomp);

		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, clone)) {
			icalcomponent_free (clone);
			g_object_unref (comp);

			return;
		}
	}

	e_cal_component_get_categories_list (comp, &categ_list);

	g_object_unref (comp);

	if (!categ_list)
		return;

	e_soap_message_start_element (msg, "Categories", NULL, NULL);

	for (citer = categ_list; citer;  citer = g_slist_next (citer)) {
		const gchar *category = citer->data;

		if (!category || !*category)
			continue;

		e_ews_message_write_string_parameter (msg, "String", NULL, category);
	}

	e_soap_message_end_element (msg); /* Categories */

	e_cal_component_free_categories_list (categ_list);
}

static void
convert_vevent_calcomp_to_xml (ESoapMessage *msg,
                               gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	ECalComponent *comp = e_cal_component_new ();
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend;
	icalproperty *prop;
	gboolean has_alarms;
	const gchar *value;

	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa564690.aspx */

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element (msg, "CalendarItem", NULL, NULL);

	/* subject */
	value = icalcomponent_get_summary (icalcomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Subject", NULL, value);

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	/* description */
	value = icalcomponent_get_description (icalcomp);
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, value, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, comp, icalcomp);

	/* set alarms */
	has_alarms = e_cal_component_has_alarms (comp);
	if (has_alarms)
		ews_set_alarm (msg, comp);
	else
		e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	/* start time, end time and meeting time zone */
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);

	ewscal_set_time (msg, "Start", &dtstart, FALSE);
	ewscal_set_time (msg, "End", &dtend, FALSE);
	/* We have to do the time zone(s) later, or the server rejects the request */

	/* All day event ? */
	if (icaltime_is_date (dtstart))
		e_ews_message_write_string_parameter (msg, "IsAllDayEvent", NULL, "true");

	/*freebusy*/
	prop = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	if (!g_strcmp0 (icalproperty_get_value_as_string (prop), "TRANSPARENT"))
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Free");
	else
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Busy");

	/* location */
	value = icalcomponent_get_location (icalcomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Location", NULL, value);

	/* collect attendees */
	e_ews_collect_attendees (icalcomp, &required, &optional, &resource);

	if (required != NULL) {
		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free (required);
	}
	if (optional != NULL) {
		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free (optional);
	}
	if (resource != NULL) {
		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free (resource);
	}
	/* end of attendees */

	/* Recurrence */
	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL) {
		ewscal_set_reccurence (msg, prop, &dtstart);
	}

	/* We have to cast these because libical puts a const pointer into the
	 * icaltimetype, but its basic read-only icaltimezone_foo() functions
	 * take a non-const pointer! */
	if (e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010)) {
		const gchar *ical_location;
		const gchar *msdn_location;
		icaltimezone *tzid;
		GSList *msdn_locations = NULL;
		GSList *tzds = NULL;

		tzid = (icaltimezone *) (dtstart.zone ? dtstart.zone : convert_data->default_zone);
		ical_location = icaltimezone_get_location (tzid);
		msdn_location = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location);

		msdn_locations = g_slist_prepend (msdn_locations, (gchar *) msdn_location);

		tzid = (icaltimezone *)
			(dtend.zone ? dtend.zone : convert_data->default_zone);
		ical_location = icaltimezone_get_location (tzid);
		msdn_location = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location);

		msdn_locations = g_slist_prepend (msdn_locations, (gchar *) msdn_location);

		msdn_locations = g_slist_reverse (msdn_locations);

		if (e_ews_connection_get_server_time_zones_sync (
				convert_data->connection,
				EWS_PRIORITY_MEDIUM,
				msdn_locations,
				&tzds,
				NULL,
				NULL)) {
			ewscal_set_timezone (msg, "StartTimeZone", tzds->data);
			ewscal_set_timezone (msg, "EndTimeZone", tzds->data);
		}

		g_slist_free (msdn_locations);
		g_slist_free_full (tzds, (GDestroyNotify) e_ews_calendar_time_zone_definition_free);
	} else {
		ewscal_set_meeting_timezone (
			msg,
			(icaltimezone *) (dtstart.zone ? dtstart.zone : convert_data->default_zone));
	}

	e_soap_message_end_element (msg); /* "CalendarItem" */
}

static void
convert_vtodo_calcomp_to_xml (ESoapMessage *msg,
                              gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	icalproperty *prop;
	icaltimetype dt;
	gint value;
	gchar buffer[16];

	e_soap_message_start_element (msg, "Task", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "Subject", NULL, icalcomponent_get_summary (icalcomp));

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, icalcomponent_get_description (icalcomp), "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icalcomp);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		ewscal_set_time (msg, "DueDate", &dt, TRUE);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		ewscal_set_time (msg, "StartDate", &dt, TRUE);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "InProgress");
			break;
		case ICAL_STATUS_COMPLETED:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "Completed");
			break;
		default:
			break;
		}
	}

	e_soap_message_end_element (msg); /* "Task" */
}

static void
convert_vjournal_calcomp_to_xml (ESoapMessage *msg,
				 gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	const gchar *text;

	e_soap_message_start_element (msg, "Message", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "ItemClass", NULL, "IPM.StickyNote");

	e_ews_message_write_string_parameter (msg, "Subject", NULL, icalcomponent_get_summary (icalcomp));

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	text = icalcomponent_get_description (icalcomp);
	if (!text || !*text)
		text = icalcomponent_get_summary (icalcomp);
	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, text, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icalcomp);

	e_soap_message_end_element (msg); /* Message */
}

void
e_cal_backend_ews_convert_calcomp_to_xml (ESoapMessage *msg,
					  gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;

	switch (icalcomponent_isa (convert_data->icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_calcomp_to_xml (msg, convert_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_calcomp_to_xml (msg, convert_data);
		break;
	case ICAL_VJOURNAL_COMPONENT:
		convert_vjournal_calcomp_to_xml (msg, convert_data);
		break;
	default:
		g_warn_if_reached ();
		break;
	}
}

static void
convert_component_categories_to_updatexml (ECalComponent *comp,
					   ESoapMessage *msg,
					   const gchar *base_elem_name)
{
	GSList *categ_list = NULL, *citer;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (msg != NULL);
	g_return_if_fail (base_elem_name != NULL);

	e_cal_component_get_categories_list (comp, &categ_list);
	e_ews_message_start_set_item_field (msg, "Categories", "item", base_elem_name);
	e_soap_message_start_element (msg, "Categories", NULL, NULL);

	for (citer = categ_list; citer;  citer = g_slist_next (citer)) {
		const gchar *category = citer->data;

		if (!category || !*category)
			continue;

		e_ews_message_write_string_parameter (msg, "String", NULL, category);
	}

	e_soap_message_end_element (msg); /* Categories */
	e_ews_message_end_set_item_field (msg);

	e_cal_component_free_categories_list (categ_list);
}

static void
convert_vevent_property_to_updatexml (ESoapMessage *msg,
                                      const gchar *name,
                                      const gchar *value,
                                      const gchar *prefix,
                                      const gchar *attr_name,
                                      const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "CalendarItem");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_vevent_component_to_updatexml (ESoapMessage *msg,
                                       gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalcomponent *icalcomp_old = e_cal_component_get_icalcomponent (convert_data->old_comp);
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend, dtstart_old, dtend_old;
	icalproperty *prop, *transp;
	const gchar *org_email_address = NULL, *value = NULL, *old_value = NULL;
	gboolean has_alarms, has_alarms_old, dt_changed = FALSE;
	gint alarm = 0, alarm_old = 0;
	gchar *recid;
	GError *error = NULL;

	/* Modifying a recurring meeting ? */
	if (icalcomponent_get_first_property (icalcomp_old, ICAL_RRULE_PROPERTY) != NULL) {
		/* A single occurrence ? */
		prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);
		if (prop != NULL) {
			recid = icalproperty_get_value_as_string_r (prop);
			e_ews_message_start_item_change (
				msg,
				E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM,
				convert_data->item_id,
				convert_data->change_key,
				e_cal_backend_ews_rid_to_index (
					convert_data->default_zone,
					recid,
					icalcomp_old,
					&error));
			g_free (recid);
		} else {
			e_ews_message_start_item_change (
				msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
				convert_data->item_id, convert_data->change_key, 0);
		}
	} else e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	/* subject */
	value = icalcomponent_get_summary (icalcomp);
	old_value = icalcomponent_get_summary (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Subject", value, "item", NULL, NULL);
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Subject", "", "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	/*description*/
	value = icalcomponent_get_description (icalcomp);
	old_value = icalcomponent_get_description (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Body", value, "item", "BodyType", "Text");
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Body", "", "item", "BodyType", "Text");

	/*update alarm items*/
	has_alarms = e_cal_component_has_alarms (convert_data->comp);
	if (has_alarms) {
		alarm = ews_get_alarm (convert_data->comp);
		has_alarms_old = e_cal_component_has_alarms (convert_data->old_comp);
		if (has_alarms_old)
			alarm_old = ews_get_alarm (convert_data->old_comp);
		if (!(alarm == alarm_old)) {
			gchar buf[20];
			snprintf (buf, 20, "%d", alarm);
			convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "true", "item", NULL, NULL);
			convert_vevent_property_to_updatexml (msg, "ReminderMinutesBeforeStart", buf, "item", NULL, NULL);
		}
	}
	else convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "false", "item", NULL, NULL);

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "CalendarItem");

	/*location*/
	value = icalcomponent_get_location (icalcomp);
	old_value = icalcomponent_get_location (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Location", value, "calendar", NULL, NULL);
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Location", "", "calendar", NULL, NULL);

	/*freebusy*/
	transp = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	value = icalproperty_get_value_as_string (transp);
	transp = icalcomponent_get_first_property (icalcomp_old, ICAL_TRANSP_PROPERTY);
	old_value = icalproperty_get_value_as_string (transp);
	if (g_strcmp0 (value, old_value)) {
		if (!g_strcmp0 (value, "TRANSPARENT"))
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Free" , "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Busy" , "calendar", NULL, NULL);
	}

	org_email_address = e_ews_collect_organizer (icalcomp);
	if (org_email_address && g_ascii_strcasecmp (org_email_address, convert_data->user_email)) {
		e_ews_message_end_item_change (msg);
		return;
	}
	/* Update other properties allowed only for meeting organizers*/
	/*meeting dates*/
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);
	dtstart_old = icalcomponent_get_dtstart (icalcomp_old);
	dtend_old = icalcomponent_get_dtend (icalcomp_old);
	if (icaltime_compare (dtstart, dtstart_old) != 0) {
		e_ews_message_start_set_item_field (msg, "Start", "calendar","CalendarItem");
		ewscal_set_time (msg, "Start", &dtstart, FALSE);
		e_ews_message_end_set_item_field (msg);
		dt_changed = TRUE;
	}

	if (icaltime_compare (dtend, dtend_old) != 0) {
		e_ews_message_start_set_item_field (msg, "End", "calendar", "CalendarItem");
		ewscal_set_time (msg, "End", &dtend, FALSE);
		e_ews_message_end_set_item_field (msg);
		dt_changed = TRUE;
	}

	/*Check for All Day Event*/
	if (dt_changed) {
		if (icaltime_is_date (dtstart))
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "true", "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "false", "calendar", NULL, NULL);
	}

	/*need to test it*/
	e_ews_collect_attendees (icalcomp, &required, &optional, &resource);
	if (required != NULL) {
		e_ews_message_start_set_item_field (msg, "RequiredAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free (required);

		e_ews_message_end_set_item_field (msg);
	}
	if (optional != NULL) {
		e_ews_message_start_set_item_field (msg, "OptionalAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free (optional);

		e_ews_message_end_set_item_field (msg);
	}
	if (resource != NULL) {
		e_ews_message_start_set_item_field (msg, "Resources", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free (resource);

		e_ews_message_end_set_item_field (msg);
	}

	/* Recurrence */
	value = NULL; old_value = NULL;
	prop = icalcomponent_get_first_property (icalcomp_old, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		old_value = icalproperty_get_value_as_string (prop);
	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		value = icalproperty_get_value_as_string (prop);

	if (prop != NULL && g_strcmp0 (value, old_value)) {
		e_ews_message_start_set_item_field (msg, "Recurrence", "calendar", "CalendarItem");
		ewscal_set_reccurence (msg, prop, &dtstart);
		e_ews_message_end_set_item_field (msg);
	}

	/* We have to cast these because libical puts a const pointer into the
	 * icaltimetype, but its basic read-only icaltimezone_foo() functions
	 * take a non-const pointer! */
	if (e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010)) {
		const gchar *ical_location;
		const gchar *msdn_location;
		icaltimezone *tzid;
		GSList *msdn_locations = NULL;
		GSList *tzds = NULL;

		if (dtstart.zone != NULL) {
			tzid = (icaltimezone *) dtstart.zone;
			ical_location = icaltimezone_get_location (tzid);
			msdn_location = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location);
			msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location);
		}

		if (dtend.zone != NULL) {
			tzid = (icaltimezone *) dtend.zone;
			ical_location = icaltimezone_get_location (tzid);
			msdn_location = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location);
			msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location);
		}

		if (e_ews_connection_get_server_time_zones_sync (
			convert_data->connection,
			EWS_PRIORITY_MEDIUM,
			msdn_locations,
			&tzds,
			NULL,
			NULL)) {
			GSList *tmp;

			tmp = tzds;
			if (dtstart.zone != NULL) {
				e_ews_message_start_set_item_field (msg, "StartTimeZone", "calendar", "CalendarItem");
				ewscal_set_timezone (msg, "StartTimeZone", tmp->data);
				e_ews_message_end_set_item_field (msg);

				/*
				 * Exchange server is smart enough to return the list of
				 * ServerTimeZone without repeated elements
				 */
				if (tmp->next != NULL)
					tmp = tmp->next;
			}

			if (dtend.zone != NULL) {
				e_ews_message_start_set_item_field (msg, "EndTimeZone", "calendar", "CalendarItem");
				ewscal_set_timezone (msg, "EndTimeZone", tmp->data);
				e_ews_message_end_set_item_field (msg);
			}
		}

		g_slist_free (msdn_locations);
		g_slist_free_full (tzds, (GDestroyNotify) e_ews_calendar_time_zone_definition_free);
	} else {
		e_ews_message_start_set_item_field (msg, "MeetingTimeZone", "calendar", "CalendarItem");
		ewscal_set_meeting_timezone (
			msg,
			(icaltimezone *) (dtstart.zone ? dtstart.zone : convert_data->default_zone));
		e_ews_message_end_set_item_field (msg);
	}

	e_ews_message_end_item_change (msg);
}

static void
convert_vtodo_property_to_updatexml (ESoapMessage *msg,
                                     const gchar *name,
                                     const gchar *value,
                                     const gchar *prefix,
                                     const gchar *attr_name,
                                     const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Task");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_vtodo_component_to_updatexml (ESoapMessage *msg,
                                      gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalproperty *prop;
	icaltimetype dt;
	gint value;
	gchar buffer[16];

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vtodo_property_to_updatexml (msg, "Subject", icalcomponent_get_summary (icalcomp), "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	convert_vtodo_property_to_updatexml (msg, "Body", icalcomponent_get_description (icalcomp), "item", "BodyType", "Text");

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		e_ews_message_start_set_item_field (msg, "DueDate", "task", "Task");
		ewscal_set_time (msg, "DueDate", &dt, TRUE);
		e_ews_message_end_set_item_field (msg);
	} else {
		e_ews_message_add_delete_item_field (msg, "DueDate", "task");
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_start_set_item_field (msg, "PercentComplete", "task", "Task");
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
		e_ews_message_end_set_item_field (msg);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		e_ews_message_start_set_item_field (msg, "StartDate", "task", "Task");
		ewscal_set_time (msg, "StartDate", &dt, TRUE);
		e_ews_message_end_set_item_field (msg);
	} else {
		e_ews_message_add_delete_item_field (msg, "StartDate", "task");
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			convert_vtodo_property_to_updatexml (msg, "Status", "InProgress", "task", NULL, NULL);
			break;
		case ICAL_STATUS_COMPLETED:
			convert_vtodo_property_to_updatexml (msg, "Status", "Completed", "task", NULL, NULL);
			break;
		case ICAL_STATUS_NONE:
		case ICAL_STATUS_NEEDSACTION:
			convert_vtodo_property_to_updatexml (msg, "Status", "NotStarted", "task", NULL, NULL);
			break;
		default:
			break;
		}
	}

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Task");

	e_ews_message_end_item_change (msg);
}

static void
convert_vjournal_property_to_updatexml (ESoapMessage *msg,
                                     const gchar *name,
                                     const gchar *value,
                                     const gchar *prefix,
                                     const gchar *attr_name,
                                     const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Message");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_vjournal_component_to_updatexml (ESoapMessage *msg,
					 gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalproperty *prop;
	const gchar *text;

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vjournal_property_to_updatexml (msg, "ItemClass", "IPM.StickyNote", "item", NULL, NULL);
	convert_vjournal_property_to_updatexml (msg, "Subject", icalcomponent_get_summary (icalcomp), "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	text = icalcomponent_get_description (icalcomp);
	if (!text || !*text)
		text = icalcomponent_get_summary (icalcomp);

	convert_vjournal_property_to_updatexml (msg, "Body", text, "item", "BodyType", "Text");

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Message");

	e_ews_message_end_item_change (msg);
}

void
e_cal_backend_ews_convert_component_to_updatexml (ESoapMessage *msg,
						  gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);

	switch (icalcomponent_isa (icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_component_to_updatexml (msg, user_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_component_to_updatexml (msg, user_data);
		break;
	case ICAL_VJOURNAL_COMPONENT:
		convert_vjournal_component_to_updatexml (msg, user_data);
		break;
	default:
		break;
	}
}

guint
e_cal_backend_ews_rid_to_index (icaltimezone *timezone,
				const gchar *rid,
				icalcomponent *comp,
				GError **error)
{
	guint index = 1;
	icalproperty *prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	struct icalrecurrencetype rule = icalproperty_get_rrule (prop);
	struct icaltimetype dtstart = icalcomponent_get_dtstart (comp);
	icalrecur_iterator * ritr;
	icaltimetype next, o_time;

	/* icalcomponent_get_datetime needs a fix to initialize ret.zone to NULL. If a timezone is not
	 * found in libical, it remains uninitialized in that function causing invalid read or crash. so
	 * we set the timezone as we cannot identify if it has a valid timezone or not */
	dtstart.zone = timezone;
	ritr = icalrecur_iterator_new (rule, dtstart);
	next = icalrecur_iterator_next (ritr);
	o_time = icaltime_from_string (rid);
	o_time.zone = dtstart.zone;

	for (; !icaltime_is_null_time (next); next = icalrecur_iterator_next (ritr), index++) {
		if (icaltime_compare_date_only (o_time, next) == 0)
			break;
	}

	icalrecur_iterator_free (ritr);

	if (icaltime_is_null_time (next)) {
		g_propagate_error (
			error, EDC_ERROR_EX (OtherError,
			"Invalid occurrence ID"));
	}

	return index;
}

void
e_cal_backend_ews_clear_reminder_is_set (ESoapMessage *msg,
					 gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;

	e_ews_message_start_item_change (
		msg,
		convert_data->change_type,
		convert_data->item_id,
		convert_data->change_key,
		convert_data->index);

	e_ews_message_start_set_item_field (msg, "ReminderIsSet","item", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);
}

void
e_cal_backend_ews_prepare_free_busy_request (ESoapMessage *msg,
					     gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	GSList *addr;
	icaltimetype t_start, t_end;
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();

	ewscal_set_availability_timezone (msg, utc_zone);

	e_soap_message_start_element (msg, "MailboxDataArray", "messages", NULL);

	for (addr = convert_data->users; addr; addr = addr->next) {
		e_soap_message_start_element (msg, "MailboxData", NULL, NULL);

		e_soap_message_start_element (msg, "Email", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "Address", NULL, addr->data);
		e_soap_message_end_element (msg); /* "Email" */

		e_ews_message_write_string_parameter (msg, "AttendeeType", NULL, "Required");
		e_ews_message_write_string_parameter (msg, "ExcludeConflicts", NULL, "false");

		e_soap_message_end_element (msg); /* "MailboxData" */
	}

	e_soap_message_end_element (msg); /* "MailboxDataArray" */

	e_soap_message_start_element (msg, "FreeBusyViewOptions", NULL, NULL);

	e_soap_message_start_element (msg, "TimeWindow", NULL, NULL);
	t_start = icaltime_from_timet_with_zone (convert_data->start, 0, utc_zone);
	t_end = icaltime_from_timet_with_zone (convert_data->end, 0, utc_zone);
	ewscal_set_time (msg, "StartTime", &t_start, FALSE);
	ewscal_set_time (msg, "EndTime", &t_end, FALSE);
	e_soap_message_end_element (msg); /* "TimeWindow" */

	e_ews_message_write_string_parameter (msg, "MergedFreeBusyIntervalInMinutes", NULL, "60");
	e_ews_message_write_string_parameter (msg, "RequestedView", NULL, "DetailedMerged");

	e_soap_message_end_element (msg); /* "FreeBusyViewOptions" */
}

void
e_cal_backend_ews_prepare_set_free_busy_status (ESoapMessage *msg,
						gpointer user_data)
{
	EwsCalendarConvertData *data = user_data;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM, data->item_id, data->change_key, 0);

	e_ews_message_start_set_item_field (msg, "LegacyFreeBusyStatus", "calendar", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Free");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);
}

void
e_cal_backend_ews_prepare_accept_item_request (ESoapMessage *msg,
					       gpointer user_data)
{
	EwsCalendarConvertData *data = user_data;
	const gchar *response_type = data->response_type;

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa566464%28v=exchg.140%29.aspx
	 * Accept and Decline meeting have same method code (10032)
	 * The real status is reflected at Attendee property PARTSTAT
	 * need to find current user as attendee and make a decision what to do.
	 * Prepare AcceptItem node in the SOAP message */

	if (response_type && !g_ascii_strcasecmp (response_type, "ACCEPTED"))
		e_soap_message_start_element (msg, "AcceptItem", NULL, NULL);
	else if (response_type && !g_ascii_strcasecmp (response_type, "DECLINED"))
		e_soap_message_start_element (msg, "DeclineItem", NULL, NULL);
	else
		e_soap_message_start_element (msg, "TentativelyAcceptItem", NULL, NULL);

	e_soap_message_start_element (msg, "ReferenceItemId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", data->item_id, NULL, NULL);
	e_soap_message_add_attribute (msg, "ChangeKey", data->change_key, NULL, NULL);
	e_soap_message_end_element (msg); /* "ReferenceItemId" */

	/* end of "AcceptItem" */
	e_soap_message_end_element (msg);
}
