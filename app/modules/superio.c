// Module for interfacing with the I2C interface

#include "module.h"
#include "lauxlib.h"
#include "platform.h"

#define SUPERIO_ADDR	0x48
#define ISPRESENT	 	0x00
#define STATIONINFO		0x30
#define BATTERY			0x31
#define STATIONINFOMIN  0x32
#define VERSION		 	0x33
#define ADC0  			0x01
#define ADC1  			0x02
#define ADC2  			0x03
#define ADCLEN  		2
#define CMD_ENABLE_A7 	0x02
#define CMD_SWITCH_ON 	0x05
#define CMD_SWITCH_OFF 	0x06

// Lua: speed = i2c.setup( sda, scl, speed )
static int superio_setup( lua_State *L )
{
unsigned sda = luaL_checkinteger( L, 1 );
unsigned scl = luaL_checkinteger( L, 2 );

  if(scl==0 || sda==0)
    return luaL_error( L, "no i2c for D0" );

  s32 speed = ( s32 )luaL_checkinteger( L, 3 );
  if (speed <= 0)
    return luaL_error( L, "wrong arg range" );
  lua_pushinteger( L, platform_i2c_setup( 0, sda, scl, (u32)speed ) );
  return 1;
}

// Lua: i2c.start( id )
static int start( lua_State *L )
{
  unsigned id = luaL_checkinteger( L, 1 );

  //MOD_CHECK_ID( i2c, id );
  platform_i2c_send_start( id );
  return 0;
}

// Lua: i2c.stop( id )
static int stop( lua_State *L )
{
  unsigned id = luaL_checkinteger( L, 1 );

  //MOD_CHECK_ID( i2c, id );
  platform_i2c_send_stop( id );
  return 0;
}

// Lua: status = i2c.address( id, address, direction )
static int address( lua_State *L, int address, int direction )
{
unsigned id = 0;

  //MOD_CHECK_ID( i2c, id );
  if ( address < 0 || address > 127 )
    return 0;

  return platform_i2c_send_address( id, (u16)address, direction );
}

// Lua: wrote = i2c.write( id, data1, [data2], ..., [datan] )
// data can be either a string, a table or an 8-bit number
static int write( lua_State *L, int addr )
{
unsigned id = 0;

  MOD_CHECK_ID( i2c, id );
  if( platform_i2c_send_byte( id, addr ) != 1 )
	  return 0;
  return 1;
}
void char2hex(char a)
{
const char *tbl="0123456789ABCDEF";

	platform_uart_send( 0, '0' );
	platform_uart_send( 0, 'x' );
	if( a < 0x10 )
		platform_uart_send( 0, '0' );
	else
		platform_uart_send( 0, tbl[(a >> 4) & 0x0F] );
	platform_uart_send( 0, tbl[a & 0x0F] );
	platform_uart_send( 0, ',' );
}


int checkCRC(unsigned short *IN)
{
unsigned char i;
unsigned short crc = 0;

	for(i = 0; i < 21; i++)
		crc ^= IN[i];

	if( IN[21] == crc )
		return 1;

	return 0;
}

//superio_read( int dev_addr, int addr, int Len )
static int superio_read( lua_State *L )
{
unsigned dev_addr	= luaL_checkinteger( L, 1 );
unsigned addr		= luaL_checkinteger( L, 2 );
u32 size			= ( u32 )luaL_checkinteger( L, 3 );
u32 i;
luaL_Buffer b;
char IN[size+1];
int data;

  //MOD_CHECK_ID( i2c, 0 );
  if( size == 0 )
    return 0;

  start( L );
  if( address( L, dev_addr, PLATFORM_I2C_DIRECTION_TRANSMITTER ) == true )
  {
	  write( L, addr );
	  stop( L );
	  start( L );
	  address( L, dev_addr, PLATFORM_I2C_DIRECTION_RECEIVER );
	  luaL_buffinit( L, &b );
	  memset(IN, 0, size + 1);
	  for( i = 0; i < size; i++ )
	  {
		if( ( data = platform_i2c_recv_byte( 0, i < size - 1 ) ) == -1 )
		  break;
		else
		{
		  luaL_addchar( &b, ( char )data );
		  IN[i] = data;
		}
	  }
	  stop( L );
	  if( size == 1 )
	  {
		  luaL_pushresult( &b );
		  return 1;
	  }else if( size == 2 )
	  {
		  data = (int)((IN[1] << 8 ) + IN[0]);
		  lua_pushinteger(L,data);
		  return 1;

	  }else
	  {
		  if( checkCRC((unsigned short*)IN) == 1 )
		  {
			  luaL_pushresult( &b );
			  return 1;
		  }
	  }

  }
  return 0;
}

//superio_write( int dev_addr, int addr, char *command )

static int superio_write( lua_State *L )
{
unsigned id = 0;
unsigned dev_addr = luaL_checkinteger( L, 1 );
const char *pdata;
size_t datalen, i;
int numdata;
u32 wrote = 0;
unsigned argn;

	  //MOD_CHECK_ID( i2c, id );
	  if( lua_gettop( L ) < 2 )
	    return luaL_error( L, "wrong arg type" );

	  start( L );
	  if( address( L, dev_addr, PLATFORM_I2C_DIRECTION_TRANSMITTER ) == true )
	  {
		  for( argn = 2; argn <= lua_gettop( L ); argn ++ )
		  {
			// lua_isnumber() would silently convert a string of digits to an integer
			// whereas here strings are handled separately.
			if( lua_type( L, argn ) == LUA_TNUMBER )
			{
			  numdata = ( int )luaL_checkinteger( L, argn );
			  if( numdata < 0 || numdata > 255 )
				return luaL_error( L, "wrong arg range" );
			  if( platform_i2c_send_byte( id, numdata ) != 1 )
				break;
			  wrote ++;
			}else if( lua_istable( L, argn ) )
			{
			  datalen = lua_objlen( L, argn );
			  for( i = 0; i < datalen; i ++ )
			  {
				lua_rawgeti( L, argn, i + 1 );
				numdata = ( int )luaL_checkinteger( L, -1 );
				lua_pop( L, 1 );
				if( numdata < 0 || numdata > 255 )
				  return luaL_error( L, "wrong arg range" );
				if( platform_i2c_send_byte( id, numdata ) == 0 )
				  break;
			  }
			  wrote += i;
			  if( i < datalen )
				break;
			}else
			{
			  pdata = luaL_checklstring( L, argn, &datalen );
			  for( i = 0; i < datalen; i ++ )
				if( platform_i2c_send_byte( id, pdata[ i ] ) == 0 )
				  break;
			  wrote += i;
			  if( i < datalen )
				break;
			}
		}
	  }
	  stop( L );
	  lua_pushinteger( L, wrote );
	  return 1;
}

//superio_isready( int dev_addr, int addr, int Len )
static int superio_isready( lua_State *L )
{
	return superio_read( L );
}

//superio_getversion( int dev_addr, int addr, int Len )
static int superio_getversion( lua_State *L )
{
	return superio_read( L );
}

// Module function map
static const LUA_REG_TYPE superio_map[] = {
  { LSTRKEY( "setup" )		, LFUNCVAL( superio_setup ) },
  { LSTRKEY( "write" )		, LFUNCVAL( superio_write ) },
  { LSTRKEY( "read" )		, LFUNCVAL( superio_read ) },
  { LSTRKEY( "isready" )	, LFUNCVAL( superio_isready ) },
  { LSTRKEY( "version" )	, LFUNCVAL( superio_getversion ) },
  { LSTRKEY( "DEV_ADDR" )	, LNUMVAL( SUPERIO_ADDR ) },
  { LSTRKEY( "VERSION" )	, LNUMVAL( VERSION ) },
  { LSTRKEY( "ISPRESENT" )	, LNUMVAL( ISPRESENT ) },
  { LSTRKEY( "STATION" )	, LNUMVAL( STATIONINFO ) },
  { LSTRKEY( "BATTERY" )	, LNUMVAL( BATTERY ) },
  { LSTRKEY( "STATION_MIN" ), LNUMVAL( STATIONINFOMIN ) },
  { LSTRKEY( "ADC0" )		, LNUMVAL( ADC0 ) },
  { LSTRKEY( "ADC1" )		, LNUMVAL( ADC1 ) },
  { LSTRKEY( "ADC2" )		, LNUMVAL( ADC2 ) },
  { LSTRKEY( "ADCLEN" )		, LNUMVAL( ADCLEN ) },
  { LSTRKEY( "ENABLE_A7" )	, LNUMVAL( CMD_ENABLE_A7 ) },
  { LSTRKEY( "SWITCH_ON" )	, LNUMVAL( CMD_SWITCH_ON ) },
  { LSTRKEY( "SWITCH_OFF" )	, LNUMVAL( CMD_SWITCH_OFF )},
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(SUPERIO, "superio", superio_map, NULL);
