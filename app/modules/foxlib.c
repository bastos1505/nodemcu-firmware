// Module for interfacing with GPIO

#include "module.h"
#include "lauxlib.h"
#include "lmem.h"
#include "platform.h"
#include "user_interface.h"
#include "c_types.h"
#include "c_string.h"
#include "driver/readline.h"
#include "c_math.h"

#include "module.h"
#include "lauxlib.h"
#include "platform.h"
#include "c_stdlib.h"

#include "lwip/ip_addr.h"
#include "espconn.h"
#include "lwip/dns.h"
#include "lwip/app/ping.h"
#include "lwip/raw.h"
#include "c_stdio.h"

// Timer constants
#define CONST_WIFI_TIMER  0
#define CONST_TIMESTAMP_TIMER 1
#define CONST_GPRS_TIMER 2
#define CONST_SLEEP_TIMER 3
#define CONST_WDT_TIMER 4
#define CONST_GENERAL_TIMER 5

// Components constants
#define CONST_SUPERIO 0x01
#define CONST_BAROMETER 0x02
#define CONST_STATION 0x04
#define CONST_GPRS 0x08
#define CONST_WIFI 0x10

// Test Sequence
#define TEST_WIFI 0
#define TEST_BMP 1
#define TEST_IO 2
#define TEST_GPRS 3
#define TEST_INFO 4
#define EXTEND_TIME 6

static lua_State *gL = NULL;
static int ping_callback_ref;
static int ping_host_count;
static ip_addr_t ping_host_ip;
const char *ping_target;

typedef struct station_frame
{
	uint16_t battery;
	uint16_t temp_inst;
	uint16_t temp_min;
	uint16_t temp_max;
	uint16_t temp_avg;
	uint16_t humi_inst;
	uint16_t humi_min;
	uint16_t humi_max;
	uint16_t humi_avg;
	uint16_t wind_inst;
	uint16_t wind_min;
	uint16_t wind_max;
	uint16_t wind_avg;
	uint16_t gust_speed;
	uint16_t wind_direction;
	uint32_t radiation;
	uint16_t uv;
	uint16_t rainfall;
	uint16_t soil_temp;
	uint16_t soil_humi;
}FRAME;

typedef struct send_frame
{						// size		POS
	uint8_t size;		// 	1		 0
	uint32_t id;		//	4		 1
	uint16_t soil_temp;	// 	2		 5
	uint16_t soil_humi;	// 	2		 7
	double lat;			// 	8		 9
	double lon;			// 	8		 17
	uint32_t timestamp;	// 	4		 25
	uint16_t rain;		//  2		 29
	uint16_t bat_gust;	//	2		 31
	uint8_t	wind_min;	//	1		 33
	uint8_t humi_min;	//	1		 34
	uint8_t wind_max;	//	1		 35
	uint8_t humi_max;	//	1		 36
	uint8_t wind_avg;	//	1		 37
	uint8_t humi_avg;	//	1		 38
	uint16_t press;		//	2		 39
	uint8_t pre_alt_dir;// 	1		 41
	uint8_t alt;		//	1		 42
	uint8_t wind_dir;	//	1		 43
	uint8_t	temp_min;	//	1		 44
	uint8_t temp_max;	//	1		 45
	uint8_t	temp_avg;	//	1		 46
	uint8_t	uv;			//	1		 47
	uint16_t light;		//	2		 48
	uint8_t	temp_light;	//	1		 50
	uint8_t uv_light;	//	1		 51
}DATA_SEND;



const int t_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
const int t_leapyear[] = {72,76,80,84,88,92,96,0,4,8,12,16,20,24,28,32,36};
const char tbl[]="0123456789ABCDEF";

char check(char IN)
{
	if( IN >= 'A' && IN <= 'F' )
		return (IN - 'A' + 10);
	else if( IN >= 'a' && IN <= 'f' )
		return (IN - 'a' + 10);

	return IN - 0x30;
}

char conv(char HI, char LO)
{
	return (char) ((check(HI) * 16) + check(LO) );
}

static int foxlib_translate( lua_State* L )
{
uint32_t len;
char *buffer = (char*)lua_tolstring( L, 1, &len );
char *ptr;
char *ptrAux;

	if( (ptr = strstr(buffer,"%")) != NULL)
	{
		while( ptr != NULL )
		{
			*ptr = conv( *(ptr + 1), *(ptr + 2) );
			ptrAux = ptr + 3;
			ptr++;
			c_strcpy(ptr,ptrAux);
			len -= 2;
			buffer[len] = 0;
			ptr = strstr(buffer,"%");
		}
	}

	lua_pushlstring(L,buffer,strlen(buffer));
	return 1;
}

int get_leapyear(int year)
{
int i;
	for( i = 0; i < 17; i++ )
	   if ( (t_leapyear[i] + 1900) == year )
		   return 1;

	for( i = 0; i < 17; i++ )
		if ( (t_leapyear[i] + 2000) == year)
			return 1;

	return 0;
}

uint32_t calcTimeStamp(const char *str_year, const char *str_month, const char *str_day,
		const char *str_hour, const char *str_minute, const char *str_second)
{
uint32_t leap = 0;
uint32_t sum_year = 0;
uint32_t sum_month = 0;

int year	= atoi(str_year);
int month	= atoi(str_month);
int day		= atoi(str_day);
int hour	= atoi(str_hour);
int minute	= atoi(str_minute);
int second	= atoi(str_second);

	if( year < 1970 || year > 2038)
	   return 0;
	if( month < 1 || month > 12 )
	   return 0;
	if( hour < 0 || hour > 23 )
	   return 0;
	if( minute < 0 || minute > 59 )
	   return 0;
	if( second < 0 || second > 59 )
	   return 0;

	if( month == 2 && day > 28 && get_leapyear(year) == 0 )
	   return 0;

	for(int y = 1970; y < year ; y++ )
	{
	   sum_year += 31536000;
	   if( get_leapyear(y) != 0 )
		   sum_year += 86400;
	}
	sum_month = (t_month[month-1]*86400);
	return (sum_year + sum_month + ((day-1)*86400) + (hour*3600) + (minute*60) + second);
}

static int foxlib_timestamp( lua_State* L )
{
uint32_t len;
const char *str_year	= lua_tolstring( L, 1, &len );
const char *str_month	= lua_tolstring( L, 2, &len );
const char *str_day		= lua_tolstring( L, 3, &len );
const char *str_hour	= lua_tolstring( L, 4, &len );
const char *str_minute	= lua_tolstring( L, 5, &len );
const char *str_second	= lua_tolstring( L, 6, &len );
uint32_t TIMESTAMP = calcTimeStamp(str_year, str_month, str_day, str_hour, str_minute, str_second);

	if( TIMESTAMP == 0 )
		return 0;

    lua_pushinteger(L, TIMESTAMP);
	return 1;
}
static int foxlib_timestampUN( lua_State* L )
{
uint32_t len;
const char *DATA = lua_tolstring( L, 1, &len );
char *str_year;
char *str_month;
char *str_day;
char *str_hour;
char *str_minute;
char *str_second;
uint32_t TIMESTAMP = 0;
char buffer[len];

	strcpy(buffer,DATA);
	str_year		= buffer;
	str_year[4]		= 0;
	str_month		= ( str_year + 5 );
	str_month[2]	= 0;
	str_day			= ( str_month + 3 );
	str_day[2]		= 0;
	str_hour		= ( str_day + 3 );
	str_hour[2]		= 0;
	str_minute		= ( str_hour + 3 );
	str_minute[2]	= 0;
	str_second		= ( str_minute + 3 );
	TIMESTAMP = calcTimeStamp(str_year, str_month, str_day, str_hour, str_minute, str_second);

	if( TIMESTAMP == 0 )
		return 0;

    lua_pushinteger(L, TIMESTAMP);
	return 1;

}


void setStationInfo(char *index, int *pos, char *template, FRAME station)
{
const char *Station = "\"f\":%d,"	//battery
		"\"g\":%d,"					//t_min
		"\"h\":%d,"					//t_max
		"\"i\":%d,"					//t_avg
		"\"j\":%d,"					//h_min
		"\"k\":%d,"					//h_max
		"\"l\":%d,"					//h_avg
		"\"m\":%d,"					//w_min
		"\"n\":%d,"					//w_max
		"\"o\":%d,"					//w_avg
		"\"p\":%d,"					//gust
		"\"q\":%d,"					//direction
		"\"r\":%d,"					//rain
		"\"s\":%d,"					//UV
		"\"t\":%d,"					//radiation
		"\"u\":%d,"					//soil_temp
		"\"v\":%d";					//soil_humi
		//"\"v\":%d,"
		//"\"w\":%d,"
		//"\"x\":%d,"
		//"\"y\":%d";

	c_sprintf(template,Station,
			station.battery,
			station.temp_max,  		station.temp_min,	station.temp_avg,
			station.humi_max, 		station.humi_min,	station.humi_avg,
			station.wind_max,		station.wind_min,	station.wind_avg,	station.gust_speed,	station.wind_direction,
			station.rainfall, 		station.uv,			station.radiation,	station.soil_temp,	station.soil_humi);
	*index = 'v';
	//*index = 'y';
	*pos = *pos + strlen(template);
}

//              1        2       3      4        5         6
//Lua enconde(  ID,  TIMESTAMP, TYPE,STATION, PRESSURE, PREFIX)
static int foxlib_encode( lua_State* L )
{
size_t tlen;
size_t plen;
char numArgs 			= lua_gettop( L );
unsigned int id 		= luaL_checkinteger( L, 1 );
unsigned int timestamp	= luaL_checkinteger( L, 2 );
unsigned int type 		= luaL_checkinteger( L, 3 );
const char *STATION		= lua_tolstring( L, 4, &tlen );
unsigned int press		= luaL_checkinteger( L, 5 );
const char *PREFIX		= lua_tolstring( L, 6, &plen );
char output[256];
char sense = 'd';
char cnt;
FRAME station;
DATA_SEND frame;
uint16_t sz;
	if( numArgs < 5 )
		return 0;

	memset(&station, 0, sizeof(FRAME));
	memset(&frame, 0, sizeof(DATA_SEND));
	memset(output,0,sizeof(output));
	memset(output,0,256);
	switch(type)
	{
	case 0:
		memcpy(&station, STATION, tlen );
		station.radiation	= ( uint32_t )((STATION[33] << 24) + (STATION[32] << 16) + (STATION[31] << 8) + STATION[30] );
		station.uv			= ( uint16_t )((STATION[35] << 8) + STATION[34]);
		station.rainfall	= ( uint16_t )(( uint16_t ) (STATION[37] << 8) + STATION[36]);
		station.soil_temp	= ( uint16_t )(( uint16_t ) (STATION[39] << 8) + STATION[38]);
		station.soil_humi	= ( uint16_t )(( uint16_t ) (STATION[41] << 8) + STATION[40]);
		c_sprintf(output,"%s\"a%db%dc%dd%de%df%dg%dh%di%dj%dk%dl%dm%dn%do%dp%dq%dr%d\"",PREFIX,
								id,timestamp,station.battery,
								station.temp_min,station.temp_max,station.temp_avg,
								station.humi_min,station.humi_max,station.humi_avg,
								station.wind_min,station.wind_max,station.wind_avg,station.gust_speed,station.wind_direction,
								station.rainfall,station.radiation,station.uv,press);
		break;
	case 1:
		c_sprintf(output,"%s\"%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d\"",PREFIX,
								id,timestamp,station.battery,
								station.temp_min,station.temp_max,station.temp_avg,
								station.humi_min,station.humi_max,station.humi_avg,
								station.wind_min,station.wind_max,station.wind_avg,station.gust_speed,station.wind_direction,
								station.rainfall,station.radiation,station.uv,station.soil_temp,station.soil_temp,press);
		break;
	default:
		break;
	}
	lua_pushlstring(L, output,strlen(output));
	return 1;
}

static int foxlib_split (lua_State *L)
{
const char *s = luaL_checkstring(L, 1);
const char *sep = luaL_checkstring(L, 2);
const char *e;
int i = 1;

	lua_newtable(L);  /* result */

	/* repeat for each separator */
	while ((e = strchr(s, *sep)) != NULL)
	{
		lua_pushlstring(L, s, e-s);  /* push substring */
		lua_rawseti(L, -2, i++);
		s = e + 1;  /* skip separator */
	}

	/* push last substring */
	lua_pushstring(L, s);
	lua_rawseti(L, -2, i);

	return 1;  /* return the table */
}

static double ln(double x)
{
double y = (x-1)/(x+1);
double y2 = y*y;
double r = 0;

	for (int8_t i=33; i>0; i-=2)
		r = 1.0/(double)i + y2 * r;
	return 2*y*r;
}

static int foxlib_altitude(lua_State *L)
{
uint32_t x = luaL_checkinteger(L,1);
double f = (double) ((double)101325/(double)x);
double alt = (double)8484.83f * ln(f);

	lua_pushinteger(L,(uint32_t)alt);
	return 1;
}

void format_send(char *IN,int size)
{
char Aux[size*2];
int i;

	for(i = 0; i < size; i++)
	{
		Aux[i*2]	= tbl[(IN[i]>>4)&0x0F];
		Aux[(i*2)+1]= tbl[IN[i]&0x0F];
	}
	memcpy(IN,Aux,2*size);
}

void ping_received(void *arg, void *data)
{
struct ping_msg *pingmsg = (struct ping_msg*)arg;
struct ping_option *pingopt = pingmsg->ping_opt;
struct ping_resp *pingresp = (struct ping_resp*)data;

//char ipaddrstr[16];
//    ip_addr_t source_ip;

//    source_ip.addr = pingopt->ip;
    //ipaddr_ntoa_r(&source_ip, ipaddrstr, sizeof(ipaddrstr));

    // if we've registered a lua callback function, retrieve
    // it from registry + call it, otherwise just print the ping
    // response in a similar way to the standard iputils ping util
    if (ping_callback_ref != LUA_NOREF)
    {
      lua_rawgeti(gL, LUA_REGISTRYINDEX, ping_callback_ref);
      lua_pushinteger(gL, pingresp->bytes);
      //lua_pushstring(gL, ipaddrstr);
      lua_pushstring(gL, ping_target);
      lua_pushinteger(gL, pingresp->seqno);
      lua_pushinteger(gL, pingresp->ttl);
      lua_pushinteger(gL, pingresp->resp_time);
      lua_call(gL, 5, 0);
    } else
    {
    	c_printf("%d bytes from %s, icmp_seq=%d ttl=%d time=%dms\n",
	       pingresp->bytes,
		   ping_target,
	       pingresp->seqno,
	       pingresp->ttl,
	       pingresp->resp_time);
    }
}

static void ping_by_hostname(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct ping_option *ping_opt = (struct ping_option *)c_zalloc(sizeof(struct ping_option));

    if (ipaddr->addr == IPADDR_NONE)
    {
      c_printf("problem resolving hostname\n");
      return;
    }

    ping_opt->count = ping_host_count;
    ping_opt->ip = ipaddr->addr;
    ping_opt->coarse_time = 0;
    ping_opt->recv_function = &ping_received;

    ping_start(ping_opt);
}

/**
  * test.ping()
  * Description:
  * 	Send ICMP ping request to address, optionally call callback when response received
  * Syntax:
  * 	wifi.sta.getconfig(ssid, password) --Set STATION configuration, Auto-connect by default, Connects to any BSSID
  *     test.ping(address)              -- send 4 ping requests to target address
  *     test.ping(address, n)           -- send n ping requests to target address
  *     test.ping(address, n, callback) -- send n ping requests to target address
  * Parameters:
  * 	address: string
  * 	n: number of requests to send
  * 	callback:
  * Returns:
  * 	Nothing.
  *
  * Example:
  *     test.ping("192.168.0.1")               -- send 4 pings to 192.168.0.1
  *     test.ping("192.168.0.1", 10)           -- send 10 pings to 192.168.0.1
  *     test.ping("192.168.0.1", 10, got_ping) -- send 10 pings to 192.168.0.1, call got_ping() with the
  *                                           --     ping results
  */
static int test_ping(lua_State *L)
{
//const char *ping_target;
unsigned count = 4;

    // retrieve address arg (mandatory)
    if (lua_isstring(L, 1))
    	ping_target = luaL_checkstring(L, 1);
    else
    	return luaL_error(L, "no address specified");


    // retrieve count arg (optional)
    if (lua_isnumber(L, 2))
    	count = luaL_checkinteger(L, 2);

    // retrieve callback arg (optional)
    if (ping_callback_ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, ping_callback_ref);

    ping_callback_ref = LUA_NOREF;

    if (lua_type(L, 3) == LUA_TFUNCTION || lua_type(L, 3) == LUA_TLIGHTFUNCTION)
      ping_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    gL = L;   // global L

    // attempt to parse ping target as IP
    uint32 ip = ipaddr_addr(ping_target);

    if (ip != IPADDR_NONE)
    {
		struct ping_option *ping_opt = (struct ping_option *)c_zalloc(sizeof(struct ping_option));
		ping_opt->count = count;
		ping_opt->ip = ip;
		ping_opt->coarse_time = 0;
		ping_opt->recv_function = &ping_received;

		ping_start(ping_opt);
	}else
	{
		ping_host_count = count;
		struct espconn *ping_dns_lookup;
		espconn_create(ping_dns_lookup);
		espconn_gethostbyname(ping_dns_lookup, ping_target, &ping_host_ip, ping_by_hostname);
    }

    return 0;
}

//            1     2        3       4        5
//Lua enconde(ID,TIMESTAMP, TYPE, STATION, PRESSURE)
static int foxlib_test(lua_State *L)
{
size_t tlen;
char numArgs 			= lua_gettop( L );
unsigned int id 		= luaL_checkinteger( L, 1 );
unsigned int timestamp	= luaL_checkinteger( L, 2 );
unsigned int type 		= luaL_checkinteger( L, 3 );
const char *STATION		= lua_tolstring( L, 4, &tlen );
unsigned int press		= luaL_checkinteger( L, 5 );
char output[256];
char sense = 'd';
char cnt;
FRAME station;
DATA_SEND frame;
uint16_t sz;
	if( numArgs < 5 )
		return 0;

	memset(&station, 0, sizeof(FRAME));
	memset(&frame, 0, sizeof(DATA_SEND));
	memset(output,0,sizeof(output));
	memset(output,0,256);
	switch(type)
	{
	case 0:
		memcpy(&station, STATION, tlen );
		station.radiation	= ( uint32_t )((STATION[33] << 24) + (STATION[32] << 16) + (STATION[31] << 8) + STATION[30] );
		station.uv			= ( uint16_t )((STATION[35] << 8) + STATION[34]);
		station.rainfall	= ( uint16_t )(( uint16_t ) (STATION[37] << 8) + STATION[36]);
		station.soil_temp	= ( uint16_t )(( uint16_t ) (STATION[39] << 8) + STATION[38]);
		station.soil_humi	= ( uint16_t )(( uint16_t ) (STATION[41] << 8) + STATION[40]);
		c_sprintf(output,"{\"token\":\"a%db%dc%dd%de%df%dg%dh%di%dj%dk%dl%dm%dn%do%dp%dq%dr%d\"}",
								id,timestamp,station.battery,
								station.temp_min,station.temp_max,station.temp_avg,
								station.humi_min,station.humi_max,station.humi_avg,
								station.wind_min,station.wind_max,station.wind_avg,station.gust_speed,station.wind_direction,
								station.rainfall,station.radiation,station.uv,press);
		break;
	case 1:
		c_sprintf(output,"{\"token\":\"%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d#%d\"}",
								id,timestamp,station.battery,
								station.temp_min,station.temp_max,station.temp_avg,
								station.humi_min,station.humi_max,station.humi_avg,
								station.wind_min,station.wind_max,station.wind_avg,station.gust_speed,station.wind_direction,
								station.rainfall,station.radiation,station.uv,station.soil_temp,station.soil_temp,press);
		break;
	default:
		break;
	}
	lua_pushlstring(L, output,strlen(output));

return 1;
}

// Module function map
static const LUA_REG_TYPE foxlib_map[] = {
  { LSTRKEY( "translate" )	,LFUNCVAL( foxlib_translate ) },
  { LSTRKEY( "timestamp" )	,LFUNCVAL( foxlib_timestamp ) },
  { LSTRKEY( "timestampUN" ),LFUNCVAL( foxlib_timestampUN ) },
  { LSTRKEY( "encode" )		,LFUNCVAL( foxlib_encode ) },
  { LSTRKEY( "split" )		,LFUNCVAL( foxlib_split ) },
  { LSTRKEY( "altitude" )	,LFUNCVAL( foxlib_altitude ) },
  { LSTRKEY( "test" )		,LFUNCVAL( foxlib_test ) },
  { LSTRKEY( "ping" )		,LFUNCVAL( test_ping ) },
  { LSTRKEY( "WIFI" )		,LNUMVAL( CONST_WIFI_TIMER ) },
  { LSTRKEY( "TIME" )		,LNUMVAL( CONST_TIMESTAMP_TIMER ) },
  { LSTRKEY( "GPRS" )		,LNUMVAL( CONST_GPRS_TIMER ) },
  { LSTRKEY( "SLEEP" )		,LNUMVAL( CONST_SLEEP_TIMER ) },
  { LSTRKEY( "WDT" )		,LNUMVAL( CONST_WDT_TIMER ) },
  { LSTRKEY( "GENERAL" )	,LNUMVAL( CONST_GENERAL_TIMER ) },
  { LSTRKEY( "S_SUPERIO" )	,LNUMVAL( CONST_SUPERIO ) },
  { LSTRKEY( "S_BAROMETER" ),LNUMVAL( CONST_BAROMETER ) },
  { LSTRKEY( "S_STATION" )	,LNUMVAL( CONST_STATION ) },
  { LSTRKEY( "S_GPRS" )		,LNUMVAL( CONST_GPRS ) },
  { LSTRKEY( "S_WIFI" )		,LNUMVAL( CONST_WIFI ) },
  { LSTRKEY( "TEST_WIFI" )	,LNUMVAL( TEST_WIFI ) },
  { LSTRKEY( "TEST_BMP" )	,LNUMVAL( TEST_BMP ) },
  { LSTRKEY( "TEST_IO" )	,LNUMVAL( TEST_IO ) },
  { LSTRKEY( "TEST_GPRS" )	,LNUMVAL( TEST_GPRS ) },
  { LSTRKEY( "TEST_INFO" )	,LNUMVAL( TEST_INFO ) },
  { LSTRKEY( "TEST_EXT" )	,LNUMVAL( EXTEND_TIME ) },
  { LNILKEY, LNILVAL }
};


NODEMCU_MODULE(FOXLIB, "foxlib", foxlib_map, NULL);
