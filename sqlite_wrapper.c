/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *  sqlite_log.c
 *	insert item into sqlite database
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2011-01-06		eagle zhou		0.1				Created
 */



#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sqlite3.h"
#include "sqlite_wrapper.h"

#ifdef APP_DEBUG
#define APP_DEBUG_PRINTF printf
#define APP_ERROR_PRINTF printf
#else
#define APP_DEBUG_PRINTF
#define APP_ERROR_PRINTF
#endif

#define MAX_CMD_SIZE	2048
#define MAX_DIG_SIZE	32			// store double/int with string

#define MAX_COLUMN_NAME_LEN		32
#define MAX_COLUMN_TYPE_LEN		32

#define InsertStrValue(pCmd,pTmpStr,tail,len) \
	{ \
		strcat(pCmd,pTmpStr); \
		strcat(pCmd,tail); \
		len=strlen(pTmpStr); \
		len+=strlen(tail);\
	}

#define InsertIntValue(pCmd,pTmpStr,value,tail,len) \
	{ \
		sprintf(pTmpStr,"%d",value); \
		strcat(pCmd,pTmpStr); \
		strcat(pCmd,tail); \
		len=strlen(pTmpStr); \
		len+=strlen(tail);\
	}

#define InsertDoubleValue(pCmd,pTmpStr,value,tail,len) \
	{ \
		/*char *gcvt(double value, int ndigit, char *buf); \
		example:  \
			a=-12345678901234.12345678; \
			gcvt(a, 12, str); \
			=> str =-1.23456789012e+13, \
			So, we will make sure sizeof(str)> ndigit !!! \
		*/ \
		/*pNoUse=gcvt(value,MAX_DIG_SIZE,pTmpStr); */\
		sprintf(pTmpStr,"%.2f",value); \
		strcat(pCmd,pTmpStr); \
		strcat(pCmd,tail); \
		len=strlen(pTmpStr); \
		len+=strlen(tail);\
	}


typedef struct
{
	int nTableIsExist;
	int nItemIsExist;
}
SQLiteCxt;

static int CallbackTableExist(void *pCxt, int argc, char **argv, char **azColName)
{
	int i;
	SQLiteCxt* pSQLCxt;

	pSQLCxt=(SQLiteCxt*)pCxt;

	APP_DEBUG_PRINTF("%s: table is exist \r\n",__FUNCTION__);
	for(i=0; i<argc; i++)
	{
		APP_DEBUG_PRINTF("%s = %s\r\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}	
	APP_DEBUG_PRINTF("\r\n");

	pSQLCxt->nTableIsExist=1;
	return 0;
}

static int CallbackTableCreate(void *pCxt, int argc, char **argv, char **azColName)
{
	int i;

	APP_DEBUG_PRINTF("%s: \r\n",__FUNCTION__);
	for(i=0; i<argc; i++)
	{
		APP_DEBUG_PRINTF("%s = %s\r\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}	
	APP_DEBUG_PRINTF("\r\n");
	return 0;
}

static int CallbackTableInsertItem(void *pCxt, int argc, char **argv, char **azColName)
{
	int i;

	APP_DEBUG_PRINTF("%s: \r\n",__FUNCTION__);
	for(i=0; i<argc; i++)
	{
		APP_DEBUG_PRINTF("%s = %s\r\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	APP_DEBUG_PRINTF("\r\n");
	return 0;
}

static int CallbackTableSelectItem(void *pCxt, int argc, char **argv, char **azColName)
{
	int i;
	SQLiteCxt* pSQLCxt;

	pSQLCxt=(SQLiteCxt*)pCxt;

	APP_DEBUG_PRINTF("%s: \r\n",__FUNCTION__);
	for(i=0; i<argc; i++)
	{
		APP_DEBUG_PRINTF("%s = %s\r\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	pSQLCxt->nItemIsExist=1;
	APP_DEBUG_PRINTF("\r\n");
	return 0;
}

int ConvertTableExistCmd(char* pCmd,char* pTableName, int nMaxLen)
{
	int noerr=1;
	int len;
	int totalLen;

	//example: select name from sqlite_master where type like 'table' and name like 'pTableName'
	pCmd[0]='\0';
	totalLen=0;
	InsertStrValue(pCmd,"select name from sqlite_master where type like 'table' and name like ", "'",len);
	totalLen+=len;
	InsertStrValue(pCmd, pTableName, "'",len);
	totalLen+=len;

	if(totalLen>nMaxLen)
	{
		APP_ERROR_PRINTF("%s: error: too long string: %d, maxsize: %d \r\n",__FUNCTION__,totalLen,nMaxLen);
		noerr=0;
	}

	APP_DEBUG_PRINTF("sqlite table exist command: %s \r\n",pCmd);
	return noerr;
}

int ConvertTableColumnCmd(char* pCmd,char* pTableName,SQLiteColumn* pColumnList,int nColumnNum, int nMaxLen)
{
	int noerr=1;
	int totalLen;
	int len;
	int i;
	char comma[]=",";
	char bracket[]=")";
	char* pSep;

	//example: create table pTableName(name varchar(10), age smallint)
	pCmd[0]='\0';
	totalLen=0;
	InsertStrValue(pCmd,"create table", " ",len);
	totalLen+=len;
	InsertStrValue(pCmd, pTableName, "(",len);
	totalLen+=len;

	if(nColumnNum<=0)
	{
		noerr=0;
		APP_ERROR_PRINTF("%s: error: column too small: %d \r\n",__FUNCTION__,nColumnNum);
		goto EXIT;
	}

	pSep=comma;
	for(i=0;i<nColumnNum;i++)
	{
		InsertStrValue(pCmd,pColumnList[i].name," ",len);
		totalLen+=len;
		if(i==(nColumnNum-1))
		{
			//the last column
			pSep=bracket;
		}
		InsertStrValue(pCmd,pColumnList[i].type,pSep,len);
		totalLen+=len;
	}

	if(totalLen>nMaxLen)
	{
		APP_ERROR_PRINTF("%s: error: too long string: %d, maxsize: %d \r\n",__FUNCTION__,totalLen,nMaxLen);
		noerr=0;
	}
EXIT:	
	APP_DEBUG_PRINTF("sqlite table column command: %s \r\n",pCmd);
	return noerr;
}

int ConvertTableInsertCmd(char* pCmd,char* pTableName,SQLiteColumn* pColumnList,int nColumnNum,int nMaxLen)
{
	int noerr=1;
	int i;
	int totalLen;
	int len,len2;
	char comma[]=",";
	char bracket[]=")";
	char* pSep;	
	char str[MAX_DIG_SIZE];
	
	//example: insert into tbl1 values('hello',10)
	pCmd[0]='\0';
	totalLen=0;
	InsertStrValue(pCmd,"insert into"," ",len);
	totalLen+=len;
	InsertStrValue(pCmd,pTableName," ",len);
	totalLen+=len;
	InsertStrValue(pCmd,"values","(",len);
	totalLen+=len;

	if(nColumnNum<=0)
	{
		noerr=0;
		APP_ERROR_PRINTF("%s: error: column too small: %d \r\n",__FUNCTION__,nColumnNum);
		goto EXIT;
	}

	pSep=comma;
	for(i=0;i<nColumnNum;i++)
	{
		len2=0;
		if(i==(nColumnNum-1))
		{
			//the last column
			pSep=bracket;
		}
		switch(pColumnList[i].eType)
		{
			case SQL_INT:
				InsertIntValue(pCmd, str, *((int*)pColumnList[i].pVal), pSep,len);
				break;
			case SQL_DOUBLE:
				InsertDoubleValue(pCmd, str, *((double*)pColumnList[i].pVal), pSep,len);
				break;
			case SQL_STRING:
				InsertStrValue(pCmd,"'",(char*)pColumnList[i].pVal,len);
				InsertStrValue(pCmd,"'",pSep,len2);
				break;
			default:
				noerr=0;
				APP_ERROR_PRINTF("%s: error: unknown sqlite type : %d \r\n",__FUNCTION__,pColumnList[i].eType);
				goto EXIT;
		}
		totalLen+=len;
		totalLen+=len2;
	}

	if(totalLen>nMaxLen)
	{
		APP_ERROR_PRINTF("%s: error: too long string: %d, maxsize: %d \r\n",__FUNCTION__,totalLen,nMaxLen);
		noerr=0;
	}
EXIT:	
	APP_DEBUG_PRINTF("sqlite table insert command: %s \r\n",pCmd);
	return noerr;
}

int ConvertTableSelectCmd(char* pCmd,char* pTableName,SQLiteColumn* pColumnList,int nColumnNum,int nMaxLen)
{
	int noerr=1;
	int i;
	int totalLen;
	int len;
	char and[]=" and ";
	char null[]="";
	char* pSep;	
	char str[MAX_DIG_SIZE];
	
	//example: select * from tbl1 where condition
	//condition: name='test.yuv' and param_id='h264_...' and width==720
	pCmd[0]='\0';
	totalLen=0;
	InsertStrValue(pCmd,"select * from"," ",len);
	totalLen+=len;
	InsertStrValue(pCmd,pTableName," ",len);
	totalLen+=len;
	InsertStrValue(pCmd,"where"," ",len);
	totalLen+=len;

	if(nColumnNum<=0)
	{
		noerr=0;
		APP_ERROR_PRINTF("%s: error: column too small: %d \r\n",__FUNCTION__,nColumnNum);
		goto EXIT;
	}

	pSep=and;
	for(i=0;i<nColumnNum;i++)
	{
		if(i==(nColumnNum-1))
		{
			//the last column
			pSep=null;
		}
		InsertStrValue(pCmd,(char*)pColumnList[i].name,"",len);
		totalLen+=len;
		switch(pColumnList[i].eType)
		{
			case SQL_INT:
				InsertStrValue(pCmd,"==","",len);
				totalLen+=len;
				InsertIntValue(pCmd, str, *((int*)pColumnList[i].pVal), pSep,len);	
				totalLen+=len;
				break;
			case SQL_DOUBLE:
				InsertStrValue(pCmd,"==","",len);
				totalLen+=len;
				InsertDoubleValue(pCmd, str, *((double*)pColumnList[i].pVal), pSep,len);
				totalLen+=len;
				break;
			case SQL_STRING:
				InsertStrValue(pCmd,"=","",len);	// or "=="
				totalLen+=len;
				InsertStrValue(pCmd,"'",(char*)pColumnList[i].pVal,len);
				totalLen+=len;
				InsertStrValue(pCmd,"'",pSep,len);
				totalLen+=len;
				break;
			default:
				noerr=0;
				APP_ERROR_PRINTF("%s: error: unknown sqlite type : %d \r\n",__FUNCTION__,pColumnList[i].eType);
				goto EXIT;
		}

	}

	if(totalLen>nMaxLen)
	{
		APP_ERROR_PRINTF("%s: error: too long string: %d, maxsize: %d \r\n",__FUNCTION__,totalLen,nMaxLen);
		noerr=0;
	}
EXIT:	
	APP_DEBUG_PRINTF("sqlite table select command: %s \r\n",pCmd);
	return noerr;
}

int CreateTable(SQLiteCxt* pSQLCxt,char* pTableName, sqlite3 * pDB,SQLiteColumn*pColumn,int nColumnNum,int nCreate, int* pIsExist)
{
	int rc;
	char *zErrMsg = 0;
	char Cmd[MAX_CMD_SIZE];
	int noerr=1;
	
	//check whether table is exist ?
	pSQLCxt->nTableIsExist=0;
	*pIsExist=0;
	
	noerr=ConvertTableExistCmd(Cmd,pTableName,MAX_CMD_SIZE);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: Convert table exist failure: \r\n",__FUNCTION__);
		//noerr=0;
		goto EXIT;		
	}
	
	//APP_DEBUG_PRINTF("check table cmd: %s \r\n",Cmd);
	rc = sqlite3_exec(pDB,Cmd, CallbackTableExist, (void*)pSQLCxt, &zErrMsg);
	if( rc!=SQLITE_OK )
	{
		APP_ERROR_PRINTF("%s: SQL(check table) error: %s \r\n",__FUNCTION__, zErrMsg);
		noerr=0;
		goto EXIT;
	}
	if(0==pSQLCxt->nTableIsExist)
	{
		if (1==nCreate)
		{
			//table is not exist, create it now.
			noerr=ConvertTableColumnCmd(Cmd,pTableName, pColumn, nColumnNum, MAX_CMD_SIZE);
			if(0==noerr)
			{
				APP_ERROR_PRINTF("%s: convert table column failure: \r\n",__FUNCTION__);
				goto EXIT;
			}
			//APP_DEBUG_PRINTF("create table cmd: %s \r\n",Cmd);
			rc = sqlite3_exec(pDB,Cmd, CallbackTableCreate, (void*)pSQLCxt, &zErrMsg);
			if( rc!=SQLITE_OK )
			{
				APP_ERROR_PRINTF("%s: SQL(create table) error: %s \r\n",__FUNCTION__, zErrMsg);
				noerr=0;
				goto EXIT;
			}
			*pIsExist=1;	
		}
		else
		{
			//table is not exist, return directly
		}
	}
	else
	{
		//table is already exist, return directly
		*pIsExist=1;		
	}

EXIT:		
	if(zErrMsg)
	{
		sqlite3_free(zErrMsg);
	}
	return noerr;
}


int SQLiteInsertNode(char* pDBName,char* pTableName,SQLiteColumn*pColumn,int nColumnNum)
{
	int noerr=1;
	sqlite3 *db=0;
	char *zErrMsg = 0;
	char * pSqlInsertCmd=0;
	int rc;
	SQLiteCxt sSQLiteCxt;	
	int IsExist=0;

	memset(&sSQLiteCxt,0,sizeof(SQLiteCxt));

	pSqlInsertCmd=malloc(MAX_CMD_SIZE);
	if(0==pSqlInsertCmd)
	{	
		APP_ERROR_PRINTF("%s: Can't malloc command buf : %d \r\n",__FUNCTION__,MAX_CMD_SIZE);
		noerr=0;
		goto EXIT;		
	}

	//open database
	rc = sqlite3_open(pDBName, &db);
	if( rc )
	{
		APP_ERROR_PRINTF("%s: Can't open database: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
		noerr=0;
		goto EXIT;
	}

	//check whether table is exist, if not exist, create it.
	noerr=CreateTable(&sSQLiteCxt, pTableName, db, pColumn,nColumnNum,1,&IsExist);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: Create table failure: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
		//noerr=0;
		goto EXIT;		
	}

	//convert node value into cmd string
	noerr=ConvertTableInsertCmd(pSqlInsertCmd,pTableName,pColumn,nColumnNum,MAX_CMD_SIZE);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: Convert cmd failure: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
		//noerr=0;
		goto EXIT;		
	}

	//execute: insert current item
	rc = sqlite3_exec(db,pSqlInsertCmd, CallbackTableInsertItem, &sSQLiteCxt, &zErrMsg);
	if( rc!=SQLITE_OK )
	{
		APP_ERROR_PRINTF("%s: SQL error: %s \r\n",__FUNCTION__, zErrMsg);
		noerr=0;
		goto EXIT;
	}

EXIT:
	
	if(pSqlInsertCmd)
	{
		free(pSqlInsertCmd);
	}
	if(zErrMsg)
	{
		sqlite3_free(zErrMsg);
	}
	if(db)
	{
		sqlite3_close(db);
	}
	return noerr;
}

int SQLiteNodeIsExist(char* pDBName,char* pTableName,SQLiteColumn*pColumn,int nColumnNum,int* pIsExist)
{
	int noerr=1;
	sqlite3 *db=0;
	char *zErrMsg = 0;
	char * pSqlSelectCmd=0;
	int rc;
	SQLiteCxt sSQLiteCxt;	
	int IsExist=0;

	*pIsExist=0;
	
	memset(&sSQLiteCxt,0,sizeof(SQLiteCxt));

	pSqlSelectCmd=malloc(MAX_CMD_SIZE);
	if(0==pSqlSelectCmd)
	{	
		APP_ERROR_PRINTF("%s: Can't malloc command buf : %d \r\n",__FUNCTION__,MAX_CMD_SIZE);
		noerr=0;
		goto EXIT;		
	}

	//open database
	rc = sqlite3_open(pDBName, &db);
	if( rc )
	{
		APP_ERROR_PRINTF("%s: Can't open database: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
		noerr=0;
		goto EXIT;
	}

	//check whether table is exist, if not exist, don't create it.
	noerr=CreateTable(&sSQLiteCxt, pTableName, db, pColumn,nColumnNum,0,&IsExist);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: Create table failure: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
		//noerr=0;
		goto EXIT;		
	}
	if(0==IsExist)
	{
		//table is not exist
	}
	else
	{
		sSQLiteCxt.nItemIsExist=0;
		//convert node value into cmd string
		noerr=ConvertTableSelectCmd(pSqlSelectCmd,pTableName,pColumn,nColumnNum,MAX_CMD_SIZE);
		if(0==noerr)
		{
			APP_ERROR_PRINTF("%s: Convert cmd failure: %s \r\n",__FUNCTION__, sqlite3_errmsg(db));
			//noerr=0;
			goto EXIT;
		}

		//execute: select current item
		rc = sqlite3_exec(db,pSqlSelectCmd, CallbackTableSelectItem, &sSQLiteCxt, &zErrMsg);
		if( rc!=SQLITE_OK )
		{
			APP_ERROR_PRINTF("%s: SQL error: %s \r\n",__FUNCTION__, zErrMsg);
			noerr=0;
			goto EXIT;
		}
		if(1==sSQLiteCxt.nItemIsExist)
		{
			*pIsExist=1;
		}
	}

EXIT:
	
	if(pSqlSelectCmd)
	{
		free(pSqlSelectCmd);
	}
	if(zErrMsg)
	{
		sqlite3_free(zErrMsg);
	}
	if(db)
	{
		sqlite3_close(db);
	}
	return noerr;	
}

