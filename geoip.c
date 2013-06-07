/*
 * Varnish-powered Geo IP lookup
 *
 * Idea and GeoIP code taken from
 * http://svn.wikia-code.com/utils/varnishhtcpd/wikia.vcl
 *
 * Cosimo, 28/05/2013
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <GeoIPCity.h>
#include <pthread.h>

#define vcl_string char

/* At Opera, we use the non-existent "A6" country
   code to identify Geo::IP failed lookups */
#define FALLBACK_COUNTRY "A6"
#define ERROR_COUNTRY "A7"

/* HTTP Header will be json-like */
#define HEADER_MAXLEN 255
#define HEADER_TMPL_COUNTRY "%s"
#define HEADER_TMPL_CITY "%s"

pthread_mutex_t geoip_mutex = PTHREAD_MUTEX_INITIALIZER;
GeoIP* gi;
GeoIPRecord *record;

/* Init GeoIP code */
void geoip_init () {
   if (!gi) {
      gi = GeoIP_open_type(GEOIP_CITY_EDITION_REV1,GEOIP_MEMORY_CACHE);
   }
}

static int geoip_lookup(vcl_string *ip, vcl_string *resolved_country, vcl_string *resolved_city) {
   pthread_mutex_lock(&geoip_mutex);

   if (!gi) {
      geoip_init();
   }
   if (!gi) {
      /* some bad thing happens, so just return an error code */
      snprintf(resolved_country, HEADER_MAXLEN, HEADER_TMPL_COUNTRY, ERROR_COUNTRY);
      snprintf(resolved_city, HEADER_MAXLEN, HEADER_TMPL_CITY, "");
   }
   else {

      record = GeoIP_record_by_addr(gi, ip);

      /* Lookup succeeded */
      if (record) {
         snprintf(resolved_country, HEADER_MAXLEN, HEADER_TMPL_COUNTRY,
            record->country_code
         );
         snprintf(resolved_city, HEADER_MAXLEN, HEADER_TMPL_CITY,
            record->city
         );
      }

      /* Failed lookup */
      else {
         snprintf(resolved_country, HEADER_MAXLEN, HEADER_TMPL_COUNTRY,
            FALLBACK_COUNTRY
         );
         snprintf(resolved_city, HEADER_MAXLEN, HEADER_TMPL_CITY, "");
      }
   }

   pthread_mutex_unlock(&geoip_mutex);

   /* always success because of error and fallback country */
   return 1;
}

static int geoip_lookup_country(vcl_string *ip, vcl_string *resolved) {

   pthread_mutex_lock(&geoip_mutex);

   if (!gi) {
      geoip_init();
   }
   if (!gi) {
      snprintf(resolved, HEADER_MAXLEN, "%s", ERROR_COUNTRY);
   }
   else {
      record = GeoIP_record_by_addr(gi, ip);

      snprintf(resolved, HEADER_MAXLEN, "%s", record
         ? record->country_code
         : FALLBACK_COUNTRY
      );
   }

   pthread_mutex_unlock(&geoip_mutex);

   /* Assume always success: we have FALLBACK_COUNTRY */
   return 1;
}

#ifdef __VCL__
/* Sets "X-Geo-IP" header with the geoip resolved information */
void vcl_geoip_set_header(const struct sess *sp) {
   vcl_string hval_country[HEADER_MAXLEN];
   vcl_string hval_city[HEADER_MAXLEN];
   vcl_string *ip = VRT_IP_string(sp, VRT_r_client_ip(sp));
   if (geoip_lookup(ip, hval_country, hval_city)) {
      VRT_SetHdr(sp, HDR_REQ, "\021X-Geo-IP-Country:", hval_country, vrt_magic_string_end);
      VRT_SetHdr(sp, HDR_REQ, "\016X-Geo-IP-City:", hval_city, vrt_magic_string_end);
   }
   else {
      /* Send an empty header */
      VRT_SetHdr(sp, HDR_REQ, "\021X-Geo-IP-Country:", "", vrt_magic_string_end);
      VRT_SetHdr(sp, HDR_REQ, "\016X-Geo-IP-City:", "", vrt_magic_string_end);
   }
}
#else
int main(int argc, char **argv) {
   vcl_string resolved_country[HEADER_MAXLEN] = "";
   vcl_string resolved_city[HEADER_MAXLEN] = "";
   if (argc == 2 && argv[1]) {
      geoip_lookup(argv[1], resolved_country, resolved_city);
   }
   printf("%s - %s\n", resolved_country, resolved_city);
   return 0;
}
#endif /* __VCL__ */

/* vim: syn=c ts=4 et sts=4 sw=4 tw=0
*/
