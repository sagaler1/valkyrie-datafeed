///////////////////////////////////////////////////////////////////////
//  Plugin.h
//  Standard header file for all AmiBroker plug-ins
//  MODERNIZED - Legacy support removed
//
//  Version 2.1a
///////////////////////////////////////////////////////////////////////
//  Copyright (c) 2001-2010 AmiBroker.com. All rights reserved. 
///////////////////////////////////////////////////////////////////////

#ifndef PLUGIN_H
#define PLUGIN_H 1
#define NOMINMAX
#include <windows.h> // <-- KAMUS UNTUK TIPE DATA WINDOWS DITAMBAHKAN DI SINI

// Possible types of plugins
#define PLUGIN_TYPE_AFL 1
#define PLUGIN_TYPE_DATA 2
#define PLUGIN_TYPE_AFL_AND_DATA 3
#define PLUGIN_TYPE_OPTIMIZER 4

// all exportable functions must have undecorated names 
#ifdef _USRDLL
#define PLUGINAPI extern "C" __declspec(dllexport)
#else
#define PLUGINAPI extern "C" __declspec(dllimport)
#endif


/////////////////////////////////////////////////////////////
//
// Structures and functions
// COMMON for all kinds of AmiBroker plugins 
//
/////////////////////////////////////////////////////////////

// 64-bit date/time integer
#define DATE_TIME_INT unsigned __int64

typedef unsigned char UBYTE;
typedef signed char SBYTE;

#define EMPTY_VAL (-1e10f)
#define IS_EMPTY( x ) ( x == EMPTY_VAL )
#define NOT_EMPTY( x ) ( x != EMPTY_VAL )

#define PIDCODE(a, b, c, d ) ( ((a)<<24) | ((b)<<16) | ((c)<<8) | (d) )

struct PluginInfo {
						int		nStructSize;
						int		nType;
						int		nVersion;
						int		nIDCode;
						char	szName[ 64 ];
						char    szVendor[ 64 ];
						int		nCertificate;
						int		nMinAmiVersion;
				};

enum { VAR_NONE, VAR_FLOAT, VAR_ARRAY, VAR_STRING, VAR_DISP };

#pragma pack( push, 2 )
typedef struct AmiVar
{
    int type;
    union 
    {
        float   val;
        float   *array;
        char    *string;
		void	*disp;
    };
} AmiVar;
#pragma pack(pop)

///////////////////////////////////////////////////
// COMMON EXPORTED FUNCTONS
PLUGINAPI int GetPluginInfo( struct PluginInfo *pInfo );
PLUGINAPI int Init(void);
PLUGINAPI int Release(void);


/////////////////////////////////////////////////////////////
//
// Structures and functions
// for DATA FEED plugins 
//
/////////////////////////////////////////////////////////////

#define DATE_EOD_MINUTES 63
#define DATE_EOD_HOURS   31
#define PERIODICITY_EOD  86400

/* codes for Notify() function */
#define REASON_DATABASE_LOADED		1
#define REASON_DATABASE_UNLOADED	2
#define REASON_SETTINGS_CHANGE		4
#define REASON_STATUS_RMBCLICK		8

#define WM_USER_STREAMING_UPDATE (WM_USER + 13000)

#define DAILY_MASK			0x000007FFffffFFC0i64

struct PackedDate {
						// lower 32 bits
						unsigned int IsFuturePad:1;
						unsigned int Reserved:5;
						unsigned int MicroSec:10;
						unsigned int MilliSec:10;
						unsigned int Second: 6;

						// higher 32 bits
                        unsigned int Minute : 6;
                        unsigned int Hour : 5;
                        unsigned int Day : 5;
                        unsigned int Month : 4;
                        unsigned int Year : 12;
                    };

union AmiDate
{
						DATE_TIME_INT	Date;
						struct PackedDate PackDate;
};

// 40-bytes 8-byte aligned
struct Quotation {
						union AmiDate DateTime;
                        float   Price;
                        float   Open;
                        float   High;
                        float   Low;
						float   Volume;
						float	OpenInterest;
						float	AuxData1;
						float	AuxData2;
                 };

#define MAX_SYMBOL_LEN 48
struct StockInfo {		
	char  ShortName[MAX_SYMBOL_LEN];
	char	AliasName[MAX_SYMBOL_LEN];
	char	WebID[MAX_SYMBOL_LEN];
	char  FullName[128];
	char  Address[128];
	char	Country[64];
	char	Currency[4];
	int		DataSource;
	int   DataLocalMode;
	int		MarketID;
	int		GroupID;
	int		IndustryID;
	int		GICS;
	int   Flags;     
	int   MoreFlags;
	float	MarginDeposit;
	float	PointValue;
	float	RoundLotSize;
	float	TickSize;
	int		Decimals;
	short	LastSplitFactor[ 2 ];
	DATE_TIME_INT		LastSplitDate;
	DATE_TIME_INT 	DividendPayDate;
	DATE_TIME_INT 	ExDividendDate;
	float	SharesFloat;
	float	SharesOut;
	float DividendPerShare;
	float BookValuePerShare;
	float PEGRatio;
	float ProfitMargin;	
	float	OperatingMargin;    
	float	OneYearTargetPrice;
	float	ReturnOnAssets;
	float	ReturnOnEquity;
	float	QtrlyRevenueGrowth;
	float	GrossProfitPerShare;
	float	SalesPerShare;
	float	EBITDAPerShare;
	float	QtrlyEarningsGrowth;
	float	InsiderHoldPercent;
	float	InstitutionHoldPercent;
	float	SharesShort;
	float	SharesShortPrevMonth;
	float	ForwardEPS;
	float	EPS;
	float	EPSEstCurrentYear;
	float	EPSEstNextYear;
	float	EPSEstNextQuarter;
	float ForwardDividendPerShare;            
	float	Beta;	
	float	OperatingCashFlow;
	float	LeveredFreeCashFlow;
	float	ReservedInternal[ 28 ];
	float	UserData[ 100 ];
};


enum { CATEGORY_MARKET, CATEGORY_GROUP, CATEGORY_SECTOR, CATEGORY_INDUSTRY, CATEGORY_WATCHLIST };

struct InfoSite {
	int			nStructSize;
	int			(*GetStockQty)( void );
	int			(*SetCategoryName)( int nCategory, int nItem, const char *pszName );
	const char *(*GetCategoryName)( int nCategory, int nItem );
	int			(*SetIndustrySector) ( int nIndustry, int nSector );
	int			(*GetIndustrySector) ( int nIndustry );
	struct StockInfo * (*AddStockNew)( const char *pszTicker );
};

#define RI_LAST				(1L<<0)
#define RI_OPEN				(1L<<1)
#define RI_HIGHLOW		(1L<<2)
#define RI_TRADEVOL		(1L<<3)
#define RI_TOTALVOL		(1L<<4)
#define RI_OPENINT		(1L<<5)
#define RI_PREVCHANGE	(1L<<6)
#define RI_BID				(1L<<7)
#define RI_ASK				(1L<<8)
#define RI_EPS				(1L<<9)
#define RI_DIVIDEND		(1L<<10)
#define RI_SHARES			(1L<<11)
#define RI_52WEEK			(1L<<12)
#define RI_DATEUPDATE	(1L<<13)
#define RI_DATECHANGE	(1L<<14)

struct RecentInfo
{
	int		nStructSize;
	char	Name[ 64 ];
	char	Exchange[8];
	int		nStatus;
	int		nBitmap;
	float	fOpen;
	float	fHigh;
	float	fLow;
	float	fLast;
	int		iTradeVol;
	int		iTotalVol;
	float	fOpenInt;
	float	fChange;
	float	fPrev;
	float	fBid;
	int		iBidSize;
	float	fAsk;
	int		iAskSize;
	float	fEPS;
	float	fDividend;
	float	fDivYield;
	int		nShares;
	float	f52WeekHigh;
	int		n52WeekHighDate;
	float	f52WeekLow;
	int		n52WeekLowDate;
	int		nDateChange;
	int		nTimeChange;
	int		nDateUpdate;
	int		nTimeUpdate;
	float	fTradeVol;
	float	fTotalVol; 
};

struct GQEContext
{
	int		nStructSize;
};

struct PluginStatus
{
	int			nStructSize;
	int			nStatusCode;
	COLORREF	clrStatusColor;
	char		szLongMessage[ 256 ];
	char		szShortMessage[ 32 ];
};

struct _IntradaySettings {
	int					TimeShift;
	int					FilterAfterHours;
	DATE_TIME_INT		SessionStart;
	DATE_TIME_INT		SessionEnd;
	int					FilterWeekends;		   
	int					DailyCompressionMode;
	DATE_TIME_INT		NightSessionStart;
	DATE_TIME_INT		NightSessionEnd;
};


struct _Workspace {
	int		DataSource;
	int		DataLocalMode;
	int		NumBars;
	int		TimeBase;
	int		ReservedB[ 8 ];
	BOOL	AllowMixedEODIntra;
	BOOL	RequestDataOnSave;
	BOOL	PadNonTradingDays;	 
	int		ReservedC;
	struct  _IntradaySettings IS;
	int		ReservedD;
};


struct PluginNotification
{
	int		nStructSize;
	int		nReason;
	LPCTSTR pszDatabasePath;
	HWND	hMainWnd;
	struct  _Workspace			*pWorkspace;
	struct  StockInfo			*pCurrentSINew;
};

// --- DATA PLUGIN FUNCTIONS ---

// Required modern function
PLUGINAPI int GetQuotesEx( LPCTSTR pszTicker, int nPeriodicity, int nLastValid, int nSize, struct Quotation *pQuotes, GQEContext *pContext );

// Optional functions
PLUGINAPI AmiVar GetExtraData( LPCTSTR pszTicker, LPCTSTR pszName, int nArraySize, int nPeriodicity, void* (*pfAlloc)(unsigned int nSize) );
PLUGINAPI int Configure( LPCTSTR pszPath, struct InfoSite *pSite );
PLUGINAPI struct RecentInfo * GetRecentInfo( LPCTSTR pszTicker );
PLUGINAPI int GetSymbolLimit( void );
PLUGINAPI int GetPluginStatus( struct PluginStatus *status );
PLUGINAPI int SetTimeBase( int nTimeBase );
PLUGINAPI int Notify( struct PluginNotification *pNotifyData );

#endif // PLUGIN_H
