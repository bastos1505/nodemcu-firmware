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

#define BUFFER_SZ 1024
#define BUFFER_MSK (BUFFER_SZ - 1)

static int gprs_clear( lua_State* L );

const char *MSG_OK		= "\r\nOK\r\n";
const char *MSG_NOK		= "\r\n+CME ERROR:";
const char *MSG_RDY		= "READY";
const char *MSG_NSIM	= "NO SIM CARD";
const char *CHECK_SIM	= "AT+CCID";
const char *IP_STATUS	= "AT+CIPSTATUS";
const char *IP_ANSWER	= "+CIPSTATUS:\r\n0,";
const char *CGATT_Q		= "AT+CGATT?";
const char *CGATT		= "AT+CGATT=1";
const char *CGATT_A		= "+CGATT:";
const char *CGDETACH	= "AT+CGATT=0";
const char *CGDCONT		= "AT+CGDCONT=1,\"IP\",\"%s\"";
const char *CGACT		= "AT+CGACT=1,1";
const char *CIFSR 		= "AT+CIFSR";
const char *GETSTATUS	= "AT";
const char *CIPSTART	= "AT+CIPSTART=\"TCP\",\"%s\",80";
const char *CIPSEND		= "AT+CIPSEND";
const char *CIPCLOSE	= "AT+CIPCLOSE";
const char *POWEROFF	= "AT+CPOF";
const char *CTZV		= "+CTZV:";
const char *CREG		= "AT+CREG?";
const char *INIT1		= "+CREG: 1";
const char *INIT5		= "+CREG: 5";
const char *IMEI		= "AT+EGMR=2,7";
const char *IMEIRESP	= "+EGMR:";
const char *GPSONOFF	= "AT+GPS=%d";
const char *GPSRD		= "AT+GPSRD";
const char *GPSRDRESP	= "$GNGGA,";//"$GPGGA,";
const char *GPSRDLOCAL	= "+GPSRD:";
const char *EOL			= "\r\n";
const char *CSTT		= "AT+CSTT=\"%s\",\"%s\",\"%s\"";
const char *HEADER		= "%s %s HTTP/1.1\r\n"
						  "HOST: %s\r\n"
						  "Content-Type: application/json; charset=utf-8\r\n"
						  "Content-Length: %d\r\n";
/*const char *HEADER		= "%s %s HTTP/1.1\r\n"
						  "HOST: %s\r\n"
						  "Content-Type: application/json; charset=utf-8\r\n"
						  "access_token: %s\r\n"
						  "Content-Length: %d\r\n";*/

const char *REGHEADER	= "POST %s HTTP/1.1\r\n"
						  "HOST: %s\r\n"
						  "Content-Type: application/json; charset=utf-8\r\n"
						  "Content-Length: %d\r\n";

const char *HTTP_OK		= "HTTP/1.1 200 OK";
const char *HTTP_NOK	= "HTTP/1.1 40";
const char *HTTP_RCV	= "+CIPRCV:";
const char *CCLK		= "AT+CCLK?";
const char *CCLK_RESP	= "+CCLK: ";
const char *SHUT		= "AT+CIPSHUT";
const char *CIICR		= "AT+CIICR";
const char *RSSI		= "AT+CSQ";
const char *HTTP_ANSWER[]= {"HTTP/1.1 20","HTTP/1.1 30","HTTP/1.1 40","HTTP/1.1 50","COMMAND NO RESPONSE"};
const char *IP_ANS[] 	= {"INITIAL", "GPRSACT", "CONNECT"};
const char *GPRS_A[]	={"\r\nOK\r\n","\r\n+CME ERROR"};

char ReadEnable = 0;

int gprs_Attach(lua_State *L);

double atof(char *IN)
{
char INTEGER[10];
char MANTISSE[20];
double factor = 10.0f;
double ret = 0.0f;
int i = 0;
char *ptrPoint = strstr(IN,".");
int len = 0;

	if( ptrPoint != NULL )
	{
		ptrPoint++;
		len = strlen(ptrPoint);
		for( i = 0; i < len; i++ )
		{
			ret += (double)((ptrPoint[i] - 0x30) / factor);
			factor *= 10;
		}
		ptrPoint -= 2;
		factor = 1;
		while( ptrPoint >= IN )
		{
			ret += (double)((*ptrPoint - 0x30) * factor);
			factor *= 10;
			ptrPoint--;
		}
	}else
	{
		len = strlen(IN);
		if( len != 0 )
		{
			ptrPoint = IN + (len - 1);
			factor = 1;
			while( ptrPoint >= IN )
			{
				ret += (double)((*ptrPoint - 0x30) * factor);
				factor *= 10;
				ptrPoint--;
			}
		}
	}

	return ret;
}

double convDECMIN(char *start, char *end)
{
double ret = 0.0f;
char DEGREES[5];
char MIN[10];
char *ptrPoint;
int sz;

	if( (ptrPoint = strstr( start, "." )) != NULL)
	{
		sz = ptrPoint - start;
		if( sz == 4 || sz == 5 )
		{
			c_memset(DEGREES,0,5);
			strncpy( DEGREES, start, sz - 2);
			sz = (end - ptrPoint) + 3;
			c_memset(MIN,0,10);
			strncpy( MIN, ptrPoint - 2, sz);
			ret = atof(DEGREES) + (atof(MIN) / 60);
			return ret;
		}
	}
	return -1.0f;
}

void prtn(const char *Aux, int len)
{
int i = 0;
	for( i = 0; i < len; i ++ )
	{
		platform_uart_send( 0, Aux[ i ] );
	}
	platform_uart_send( 0, EOL[ 0 ] );
	platform_uart_send( 0, EOL[ 1 ] );
}

void milisec(uint32_t tempo)
{
uint32_t t = system_get_time() + (tempo*1000);
	while(t > system_get_time() )
		system_soft_wdt_feed ();
}

char STRCMP(char *buff1,char *buff2)
{
	if( buff1 == NULL || buff2 == NULL)
		return -1;

	while(*buff1 && *buff2)
	{
		if(*buff1 != *buff2)
			return 1;
		buff1++;
		buff2++;
	}
	return 0;
}

int waitAnswer(uint32_t TIMEOUT, const char *MSG_TRUE, const char *MSG_FALSE)
{
uint32_t now = 0;
char ret[BUFFER_SZ];
int i = 0;
	c_memset(ret,0,BUFFER_SZ);
	now = system_get_time() + TIMEOUT;
	while( now > system_get_time() )
	{
		system_soft_wdt_feed ();
		if( uart_getc(&ret[i & BUFFER_MSK]) )
		{
			if( ret[i] == 0 )
				i--;

			i++;
			if( i >= c_strlen(MSG_TRUE) || i >= c_strlen(MSG_FALSE) )
			{
				milisec(1);
				if( STRCMP((char*)MSG_TRUE, (char*)HTTP_OK) != 0 )
				{
					if( c_strstr(ret,MSG_TRUE) != NULL )
						return 1;
					else if( c_strstr(ret,MSG_FALSE) != NULL )
						return 2;
				}else
				{
					if( c_strstr(ret,HTTP_ANSWER[0]) != NULL )
						return 1;
					else if( c_strstr(ret,HTTP_ANSWER[1]) != NULL )
						return 3;
					else if( c_strstr(ret,HTTP_ANSWER[2]) != NULL )
						return 4;
					else if( c_strstr(ret,HTTP_ANSWER[3]) != NULL )
						return 5;
					else if( c_strstr(ret,HTTP_ANSWER[4]) != NULL )
						return 6;
					else if( c_strstr(ret,MSG_FALSE) != NULL )
						return 2;
				}
			}
		}
	}
	milisec(1);
	return 0;
}

unsigned char sendCommand(const char *COMMAND, const char *OK, const char *NOK, int len, uint32_t TIMEOUT)
{
	prtn(COMMAND,len);
	return waitAnswer(TIMEOUT, OK, NOK );
}

static int sendCommonCommand( lua_State *L, const char *COMMAND )
{
uint32_t interval = luaL_checkinteger(L, 1)*1000000;
char ret = 0;
	if( lua_type( L, 1 ) == LUA_TNUMBER )
	{
		ret = sendCommand((char*)COMMAND, MSG_OK, MSG_NOK, strlen(COMMAND), interval);
		if( ret == 2 || ret == 1 )
			return ret;
	}/*else
		return luaL_error( L, "wrong arg range" );*/

	return 0;
}

char *readCommand(const char *COMMAND, const char *EOM1, const char *EOM2, const char *STARTCOMP, const char *ENDCOMP, lua_State *L)
{
size_t i;
uint32_t now = 0;
char ret[BUFFER_SZ];
char *ptr;
char *end;
size_t TIMEOUT = luaL_checkinteger(L, 1);
int min_sz = 0;
//size_t echo = luaL_checkinteger(L, 2);

	if( COMMAND != NULL )
		prtn(COMMAND, strlen(COMMAND));

	now = system_get_time() + (TIMEOUT * 1000000);
	i = 0;
	min_sz = strlen(EOM1);
	c_memset(ret, 0 , BUFFER_SZ);
	while( now > system_get_time() )
	{
		system_soft_wdt_feed ();
		if( uart_getc(&ret[i & BUFFER_MSK]) )
		{
			if( ret[i] == 0 )
				i--;
			i++;
			if( i >= min_sz )
			{
				if( c_strstr(ret,EOM1) != NULL  || c_strstr(ret,EOM2) != NULL)
				{
					if( (ptr = c_strstr(ret,STARTCOMP)) != NULL )
					{
						ptr += strlen(STARTCOMP);
						if( (end = c_strstr(ptr,ENDCOMP)) != NULL )
						{
							*end = 0;
							return ptr;
						}
					}else
						break;
				}
			}
		}
	}
	return NULL;
}
static int gprs_flushbuffer( lua_State* L )
{
char ret;
uint32_t now = 0;
size_t TIMEOUT = luaL_checkinteger(L, 1);

	now = system_get_time() + (TIMEOUT * 1000000);
	while( now > system_get_time() )
	{
		system_soft_wdt_feed ();
		uart_getc(&ret);
	}

	return 0;

}

/*
 * Return:	1 --> ok
 * 			2 --> nok
 * 			0 --> timeout
 */
static int gprs_isReady( lua_State* L )
{
char time;
size_t TIMEOUT = luaL_checkinteger(L, 1) * 1000000;

	time = (int)waitAnswer(TIMEOUT, MSG_RDY, MSG_NSIM);
	if( time == 0 )
		time = gprs_Attach(L);

	if( time == -1 )
		time = 0;

	lua_pushinteger(L,time);

	return 1;
}

static int gprs_getTimeStamp( lua_State* L )
{
char *tm;

	if( (tm = readCommand(CCLK, MSG_OK, MSG_NOK, CCLK_RESP, MSG_OK, L)) != NULL )//if( (time = readCommand(CCLK, MSG_OK, MSG_NOK, "+CCLK: ", "\r",L)) != NULL )
	{
		lua_pushlstring(L,tm+1, 17);
		return 1;
	}
	return 0;
}

static int gprs_getimei( lua_State* L )
{
char *imei;

	if( (imei = readCommand(IMEI, MSG_OK, MSG_OK, IMEIRESP, MSG_OK, L)) != NULL )
	{
		imei[strlen(imei) - 2] = 0;
		lua_pushlstring(L, imei, strlen(imei));
		return 1;
	}
	return 0;
}

char *gprs_ReadCommand(const char *COMMAND,const char *STARTCOMP, const char *ENDCOMP, lua_State *L)
{
size_t i;
uint32_t now = 0;
char ret[BUFFER_SZ];
char *ptr;
char *end;
size_t TIMEOUT = luaL_checkinteger(L, 1);

	if( COMMAND == NULL )
		return NULL;

	prtn(COMMAND, strlen(COMMAND));
	now = system_get_time() + (TIMEOUT * 1000000);
	i = 0;
	c_memset(ret, 0 , BUFFER_SZ);
	while( now > system_get_time() )
	{
		system_soft_wdt_feed ();
		if( uart_getc(&ret[i & 0x1FF]) )
		{
			if( ret[i] == 0 ) i--;
			i++;
			if( strstr(ret,GPRS_A[0]) != NULL )
			{
				if( (ptr = strstr(ret,STARTCOMP)) != NULL )
				{
					ptr += strlen(STARTCOMP);
					if( (end = strstr(ptr,ENDCOMP)) != NULL )
					{
						*end = 0;
						return ptr;
					}
				}else
					break;
			}else if( strstr(ret,GPRS_A[1]) != NULL )
				return (char*)GPRS_A[1];
		}
	}
return NULL;
}

int tcpip_getIpStatus(lua_State *L)
{
char *ret = gprs_ReadCommand(IP_STATUS, IP_ANSWER, MSG_OK, L);
int i;
	for(i = 2; i >= 0; i--)
	{
		if( c_strstr(ret,IP_ANS[i]) != NULL)
			return i;
	}
return -1;
}

int tcpip_getIp(lua_State *L)
{
char *ret = gprs_ReadCommand(CIFSR, EOL, EOL, L);
char *ptr;
int i = 0;
	ptr = c_strstr(ret,".");
	while( ptr != NULL )
	{
		i++;
		ptr = c_strstr(ptr+1,".");
	}
	if( i == 3 )
		return 1;

return 0;
}

int gprs_Attach(lua_State *L)
{
char *ret = gprs_ReadCommand(CGATT_Q, CGATT_A, EOL, L);
int i;
		if( c_strcmp(ret,"1") == 0)
			return 1;
		else if( c_strcmp(ret,"0") == 0)
			return sendCommonCommand(L,CGATT);
return -1;
}

int gprs_Activate(lua_State *L, char *CONTEXT)
{
int ret = -1;
	if( (ret = sendCommonCommand(L,CONTEXT)) == 1 )
	{
		system_soft_wdt_feed ();
		//tcpip_getIpStatus(L);
		ret = sendCommonCommand(L,CGACT);
	}

return ret;
}

static int gprs_start( lua_State* L )
{
uint32_t len;
int timeout = luaL_checkinteger(L, 1);
const char *APN[10];
char *APN_USED;
char Param[BUFFER_SZ];
uint8_t i = 0;
uint8_t j = 5;
uint8_t result = 1;
unsigned char cont = lua_gettop(L) - 1;

	APN[0] = lua_tolstring( L, 2, &len );
	APN[1] = lua_tolstring( L, 3, &len );
	APN[2] = lua_tolstring( L, 4, &len );
	APN[3] = lua_tolstring( L, 5, &len );
	APN[4] = lua_tolstring( L, 6, &len );
	APN[5] = lua_tolstring( L, 7, &len );
	APN[6] = lua_tolstring( L, 8, &len );
	APN[7] = lua_tolstring( L, 9, &len );
	APN[8] = lua_tolstring( L, 10, &len );
	APN[9] = lua_tolstring( L, 11, &len );
	c_memset(Param, 0 , BUFFER_SZ);
	if( APN[0] != NULL && APN[1] != NULL && APN[2] != NULL && APN[3] != NULL )
	{
		result = 1;
		gprs_clear(L);
		milisec(1500);
		tcpip_getIpStatus(L);
		len = gprs_Attach(L);
		if( len == 1 )
		{
			result = 2;
			for(i = 0; i < 5; i++)
			{
				gprs_clear(L);
				//tcpip_getIpStatus(L);
				if( APN[9] != NULL )
				{
					APN_USED = (char*)APN[9];
					c_sprintf(Param, CGDCONT, APN[9]);
					for(j = 0; j < 5; j++)
					{
						len = gprs_Activate(L,Param);
						if( len == 1 )
							goto end_act;

						milisec(1000);
					}
				}
				if( j>= 5 )
				{
					for(j = 0; j < cont; j++)
					{
						if( APN[j] != NULL )
						{
							APN_USED = (char*)APN[j];
							c_sprintf(Param, CGDCONT, APN[j]);
							len = gprs_Activate(L,Param);
							if( len == 1 )
								goto end_act;

							milisec(1000);
						}
					}
				}
			}
end_act:
			if( tcpip_getIpStatus(L) == 1 )
			{
				tcpip_getIp(L);
				tcpip_getIpStatus(L);
				result = 0;
				lua_pushinteger(L, result);
				lua_pushstring(L,APN_USED);
				return 2;
			}
		}
	}

	lua_pushinteger(L, result);
	return 1;
}

int gprs_StartSocket(lua_State *L)
{
int timeout = luaL_checkinteger(L, 1)*1000000;
const char *URL = luaL_checkstring( L, 2 );
char Aux[BUFFER_SZ];
char cont;
	if( URL == NULL )
		return 0;
	c_memset(Aux,0,BUFFER_SZ);
	c_sprintf(Aux, CIPSTART, URL );
	sendCommand(GETSTATUS, MSG_OK, MSG_NOK, 2, timeout);
	sendCommand(GETSTATUS, MSG_OK, MSG_NOK, 2, timeout);
	//milisec(1000);
	for( cont = 0; cont < 5; cont++ )
	{
		if( sendCommonCommand(L, Aux) == 1 )
			return 1;
		else
			sendCommand(CIPCLOSE,MSG_OK,MSG_NOK,strlen(CIPCLOSE),2000000);
		milisec(500);
	}
	return 0;
}

unsigned char gprs_SendHeader(lua_State *L, char *REQ)
{
int timeout = luaL_checkinteger(L, 1)*1000000;
const char *URL = luaL_checkstring( L, 2 );
const char *PATH = luaL_checkstring( L, 3 );
const char *DATA = luaL_checkstring( L, 4 );
char Aux[BUFFER_SZ];

	c_memset(Aux,0,BUFFER_SZ);
	c_sprintf(Aux, HEADER,REQ, PATH, URL, strlen(DATA) );
	if( sendCommand(CIPSEND, ">", ">", strlen(CIPSEND), timeout) == 1 )
	{
		sendCommand(Aux, EOL, EOL, strlen(Aux), timeout);
		return 1;
	}

	return 0;
}

int gprs_SendSocket( lua_State *L, char *REQ)
{
int timeout = luaL_checkinteger(L, 1)*1000000;
const char *DATA = luaL_checkstring( L, 4 );

	if( gprs_SendHeader(L, REQ) == 1 )
	{
		sendCommand(DATA, EOL, EOL, strlen(DATA), timeout);
		prtn("\x1A",1);
		return waitAnswer(3*timeout, MSG_OK, MSG_NOK );
	}

return 0;
}

//Lua send(TIMEOUT, URL, PATH, DATA)
/*
 * Return:
 * 		0 --> OK
 * 		2 --> false
 * 		3 --> http 30x
 * 		4 --> http 40x
 * 		5 --> http 50x
 */
static int gprs_send( lua_State* L )
{
uint8_t result = 3;
int timeout = luaL_checkinteger(L, 1)*1000000;

	if( gprs_StartSocket(L) == 1 )
	{
		result = 4;
		if( gprs_SendSocket(L,"POST") == 1 )
		{
			if( (result = waitAnswer(3*timeout, HTTP_OK, HTTP_NOK )) == 1 )
			{
				sendCommand((char*)CIPCLOSE, MSG_OK, MSG_NOK, strlen(CIPCLOSE), timeout/10);
				result = 0;
			}
		}
	}
	lua_pushinteger(L,result);
	return 1;
}

void readFrame(const char *Eof,const char *Eom, const char *Som, char *RET,unsigned int SZ, size_t TIMEOUT)
{
uint32_t now = 0;
char *ptr;
char *end;
int min_sz = 0;
size_t i = 0;
char ret[BUFFER_SZ];

	if( Eof != NULL && Eom != NULL && Som != NULL && RET != NULL && SZ != 0)
	{
		now = system_get_time() + (TIMEOUT * 1000000);
		min_sz = strlen(Eof);
		c_memset(RET, 0 , SZ);
		while( now > system_get_time() )
		{
			system_soft_wdt_feed ();
			if( uart_getc(&ret[i & 0x1FF]) )
			{
				if( ret[i] == 0 )
					i--;
				i++;
				if( i >= min_sz )
				{
					if( c_strstr(ret,Eof) != NULL )
					{
						if( (ptr = c_strstr(ret,Som)) != NULL )
						{
							ptr += strlen(Som);
							if( (end = c_strstr(ptr,Eom)) != NULL )
							{
								*end = 0;
								c_strcpy(RET,ptr);
								return;
							}
						}else
							return;
					}
				}
			}
		}
	}
}


//gprs.register(_10S, URL, PATH, DATA)
static int gprs_register( lua_State* L )
{
uint32_t timeout = luaL_checkinteger(L, 1) * 1000000;
char reg[BUFFER_SZ];

	if( gprs_StartSocket(L) == 1 )
	{
		if( gprs_SendSocket(L,"POST") == 1 )
		{
			if( waitAnswer(3*timeout, HTTP_OK, HTTP_NOK ) == 1 )
			{
				readFrame("}\r\n","}\r\n","\n{",reg,BUFFER_SZ,timeout);
				if( strlen(reg) > 0 )
				{
					lua_pushstring(L, reg);
					return 1;
				}
			}
		}
	}
	return 0;
}

static int gprs_readTimeStamp( lua_State* L )
{
int timeout = luaL_checkinteger(L, 1)*1000000;
uint8_t result = 3;
char *ret;

	if( gprs_StartSocket(L) == 1 )
	{
		result = 4;
		if( gprs_SendSocket(L,"GET") == 1 )
		{
			//if( (ret = readCommand(NULL,"}\r\n","}\r\n","{\"time\":","}\r\n",L)) != NULL )
			if( (ret = readCommand(NULL,"}\r\n","}\r\n","{","}\r\n",L)) != NULL )
			{
				char saida[512];
				memset(saida,0,512);
				c_memcpy(saida,ret,c_strlen(ret));
				sendCommand((char*)CIPCLOSE, MSG_OK, MSG_NOK, strlen(CIPCLOSE), timeout/10);
				//lua_pushlstring(L,tm, 10);
				lua_pushlstring(L,saida, c_strlen(saida));
				lua_pushinteger(L,0);
				return 2;
			}else
				result = 5;
		}
	}
	lua_pushinteger(L,result);
	return 1;
}

static int gprs_poweroff( lua_State* L )
{
	if( sendCommonCommand(L, POWEROFF) == 1 )
	{
		lua_pushinteger(L, 1);
		return 1;
	}
	return 0;
}

static int gprs_clear( lua_State* L )
{
uint32_t timeout = luaL_checkinteger(L, 1);
	timeout *= 1000000;
	return sendCommand(EOL, EOL, MSG_NOK, 2, timeout);
}

//lua gprs.flush(TIMEOUT, REPETITION)
static int gprs_flush( lua_State* L )
{
uint32_t timeout = luaL_checkinteger(L, 1) * 1000000;
uint32_t rep = luaL_checkinteger(L, 2);
uint16_t i;
	for(i = 0; i < rep; i++)
	{
		if(sendCommand(GETSTATUS, MSG_OK, MSG_NOK, 2, timeout) == 1 )
		{
			lua_pushinteger(L, 1);
			return 1;
		}
	}
	return 0;
}

static int gprs_rssi( lua_State* L )
{
uint32_t timeout = luaL_checkinteger(L, 1) * 1000000;
uint32_t now = system_get_time() + timeout;
uint32_t  i = 0;
char Aux[BUFFER_SZ];
char *ptr;
char *ret;
char *ptrEnd;
int16_t val = 0;
	c_memset(Aux, 0, BUFFER_SZ);
	prtn(RSSI,strlen(RSSI));
	while( now > system_get_time() )
	{
		system_soft_wdt_feed ();
		if( uart_getc(&Aux[i & BUFFER_MSK]) )
		{
			i++;
			if( (ptrEnd = strstr(Aux,MSG_OK)) != NULL )
			{
				if( (ptr = strstr(Aux,"+CSQ: ")) != NULL )
				{
					ret = strstr(ptr," ") + 1;
					ptrEnd = strstr(ret,",");
					*ptrEnd = 0;
					val = (2*atoi(ret)) - 113;
					lua_pushinteger(L, val );
					if( val < -100 )
						lua_pushstring(L,  "Poor");
					else if( val <= -91 )
						lua_pushstring(L,  "Low");
					else if( val <=-76 )
						lua_pushstring(L,  "Medium");
					else
						lua_pushstring(L,  "High");
					return 2;
				}
			}
		}else if( (ptrEnd = strstr(Aux,MSG_NOK)) != NULL )
			break;
	}
	return 0;
}



/*
 * GPS
 * */

static int gprs_switchgps( lua_State* L )
{
size_t status = luaL_checkinteger( L, 2 );
char buffer[9];

	buffer[8] = 0;
	c_sprintf(buffer,GPSONOFF,status);
	if( sendCommonCommand(L, buffer) == 1 )
	{
		lua_pushinteger(L, 1);
		return 1;
	}
	return 0;
}

char split_pos(char *IN, char OUT[][12])
{
char *ptr;
int i = 0;
int j = 0;

	ptr = IN;
	while(*ptr != 0)
	{
		if( *ptr != ',' )
		{
			OUT[i][j] = *ptr;
			j++;
		}else
		{
			i++;
			j = 0;
		}
		ptr++;
	}

return (char)i;
}


/*
 *      0       1          2     3      4     5 6 7  8    9   10  11 12 13 14
	 $GNGGA,121820.000,2524.7126,S,04917.5765,W,1,8,1.51,983.0,M,0.0,M,,*73
*/
int parse_gps(char *position, double *LAT, double *LONG, double *ALT, double *DOP)
{
char *ptrEnd;
char *ptrStart;
char splited[15][12];

	if( (ptrStart = c_strstr(position,GPSRDRESP)) != NULL)
	{
		//ptrEnd = ptrStart + strlen(ptrStart) - 1;
		c_memset(splited,0,180);

		if( split_pos(ptrStart,&splited[0]) != 0 )
		{
			if( splited[2][0] != 0 && splited[4][0] != 0 && splited[9][0] != 0)
			{
				ptrStart	= &splited[2][0];
				ptrEnd		= &splited[2][strlen(splited[2])];
				if( (*LAT = convDECMIN( ptrStart, ptrEnd - 1 )) != -1.0f )
				{
					if( splited[3][0] == 'S' )
						*LAT = -(*LAT);

					ptrStart	= &splited[4][0];
					ptrEnd		= &splited[4][strlen(splited[2])];
					if( (*LONG = convDECMIN( ptrStart, ptrEnd - 1 )) != -1.0f )
					{
						if( splited[5][0] == 'W' )
							*LONG = -(*LONG);
					}
					*DOP = atof(splited[8]);
					*ALT = atof(splited[9]);
					return 4;
				}
			}
		}
	}
	return 0;
}

static int gprs_getlocation( lua_State* L )
{
size_t len;
//uint32_t timeout	= luaL_checkinteger(L, 1)*1000;
uint32_t period		= luaL_checkinteger(L, 3);
char *position;
double LAT	= 0.0f;
double LONG	= 0.0f;
double ALT	= 0.0f;
double DOP	= 0.0f;
char saida[15];
char cmd[15];
int ret = 0;

	/*
	 *  $GPGGA,180857.000,2524.83983,S,04917.42591,W,1,03,3.1,0.0,M,,M,,0000*47
		$GPGSA,A,2,09,23,22,,,,,,,,,,3.3,3.1,1.0*38
		$GPGSV,3,1,09,09,37,236,26,23,53,188,34,22,49,061,28,30,02,319,*77
		$GPGSV,3,2,09,26,20,117,,07,36,317,,06,18,230,,03,73,108,*77
		$GPGSV,3,3,09,01,29,352,*4E
		$GPRMC,180857.000,A,2524.83983,S,04917.42591,W,0.70,315.11,120617,,,A*69
		$GPVTG,315.11,T,,M,0.70,N,1.30,K,A*3F

	 * */
	if (ReadEnable == 0)
	{
		memset(cmd,0,15);
		c_sprintf(cmd,"%s=%d",GPSRD,period);
		if( sendCommonCommand(L,cmd) == 1)
			ReadEnable = 1;
	}

	//if( sendCommonCommand(L,cmd) == 1)

	if( (position = readCommand(NULL, "$GNACC", "$GNACC", GPSRDLOCAL, EOL, L)) != NULL )
	{
		ret = parse_gps(position, &LAT, &LONG, &ALT, &DOP);
		if( ret ==  4 )
		{
			c_memset(saida,0,15);
			c_sprintf(saida,"%.6f",LAT);
			lua_pushstring(L, saida);
			c_memset(saida,0,15);
			c_sprintf(saida,"%.6f",LONG);
			lua_pushstring(L, saida);
			c_memset(saida,0,15);
			c_sprintf(saida,"%.1f",ALT);
			lua_pushstring(L, saida);
			c_memset(saida,0,15);
			c_sprintf(saida,"%.1f",DOP);
			lua_pushstring(L, saida);
			return 4;  // 4 é o número de valores a serem retornados
		}
	}


	return 0;
}

static int gprs_test( lua_State* L )
{
const char *NMEA = luaL_checkstring( L, 1 );
double LAT;
double LONG;
double ALT;
double DOP;
char saida[15];
int ret = 0;

	ret = parse_gps((char*)NMEA, &LAT, &LONG, &ALT, &DOP);

	if( ret ==  4 )
	{
		c_memset(saida,0,15);
		c_sprintf(saida,"%.06f",LAT);
		lua_pushstring(L, saida);
		c_memset(saida,0,15);
		c_sprintf(saida,"%.06f",LONG);
		lua_pushstring(L, saida);
		c_memset(saida,0,15);
		c_sprintf(saida,"%.01f",ALT);
		lua_pushstring(L, saida);
		c_memset(saida,0,15);
		c_sprintf(saida,"%.01f",DOP);
		lua_pushstring(L, saida);
		return 4;  // 4 é o número de valores a serem retornados
	}
return 0;
}



// Module function map
static const LUA_REG_TYPE gprs_map[] = {
  { LSTRKEY( "isReady" )	,LFUNCVAL( gprs_isReady ) },
  { LSTRKEY( "timestamp" )	,LFUNCVAL( gprs_getTimeStamp ) },
  { LSTRKEY( "readtimestamp" ),LFUNCVAL( gprs_readTimeStamp ) },
  { LSTRKEY( "start" )		,LFUNCVAL( gprs_start ) },
  { LSTRKEY( "send" )		,LFUNCVAL( gprs_send ) },
  { LSTRKEY( "poweroff" )	,LFUNCVAL( gprs_poweroff ) },
  { LSTRKEY( "getimei" )	,LFUNCVAL( gprs_getimei ) },
  { LSTRKEY( "clear" )		,LFUNCVAL( gprs_clear ) },
  { LSTRKEY( "flush" )		,LFUNCVAL( gprs_flush ) },
  { LSTRKEY( "flushbuffer" ),LFUNCVAL( gprs_flushbuffer ) },
  { LSTRKEY( "register" )	,LFUNCVAL( gprs_register ) },
  { LSTRKEY( "rssi" )		,LFUNCVAL( gprs_rssi ) },
  { LSTRKEY( "test" )		,LFUNCVAL( gprs_test ) },
  { LSTRKEY( "switchgps" )	,LFUNCVAL( gprs_switchgps ) },
  { LSTRKEY( "getlocation" ),LFUNCVAL( gprs_getlocation ) },
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(GPRS, "gprs", gprs_map, NULL);
