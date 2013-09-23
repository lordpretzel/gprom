#ifndef NODETYPE_H
#define NODETYPE_H

#include <stdarg.h>

#include "common.h"


typedef enum NodeTag {
    T_Invalid=0,

    /* lists */
    T_List,
    T_IntList,

    /* expression nodes */
    T_Constant,
    T_AttributeReference,
    T_FunctionCall,
    T_Operator,

    /* query block model nodes */
    T_QueryBlock,
    T_SelectItem,
    T_FromItem,
    T_FromTableRef,
    T_FromSubquery,
    T_FromJoinExpr,
    T_DistinctClause,

    /* query operator model nodes */
    T_QueryOperator,
    T_SelectionOperator,
    T_ProjectionOperator,
    T_JoinOperator,
    T_AggregationOperator,
    T_ProvenanceComputation

} NodeTag;

typedef struct Node{
    NodeTag type;
} Node;


/*stringinfo provides the string data type*/

typedef struct StringInfoData
{
    char *data;
    int  len;
    int maxlen;
    int cursor;

}StringInfoData;

typedef StringInfoData *StringInfo;


/*------------------------------------------------------------------
*makeStringInfo
*The function is create an empty StringInfoData and return a pointer.
*-------------------------------------------------------------------*/
extern StringInfo makeStringInfo(void);

/*------------------------------------------------------------------
*initStringInfo
*The function is init a StringInfoData.
*-------------------------------------------------------------------*/
extern void initStringInfo(StringInfo str);

/*------------------------------------------------------------------
*resetStringInfo
*The function is clear the current content of StringInfo.
*-------------------------------------------------------------------*/
extern void resetStringInfo(StringInfo str);

/*------------------------------------------------------------------
*appendStringInfoString
*The function is append a string to str.
*-------------------------------------------------------------------*/
extern void appendStringInfoString(StringInfo str, const char *s);

/*------------------------------------------------------------------
* The function is append to a StringInfo using a format string and a variable
* length parameter list.
*-------------------------------------------------------------------*/
extern void appendStringInfo(StringInfo str, const char *format, ...);


/*------------------------------------------------------------------
*appendStringInfoChar
*The function is append byte to str.
*-------------------------------------------------------------------*/
extern void appendStringInfoChar(StringInfo str, char ch);

/*------------------------------------------------------------------
*appendBinaryStringInfo
*The function is append binary data to a StringInfo.
*-------------------------------------------------------------------*/
extern void appendBinaryStringInfo(StringInfo str, const char *data, int datalen);

/*------------------------------------------------------------------
*enlargeStringInfo
*The function is StringInfo's buffer can hold the "needed" bytes.
*-------------------------------------------------------------------*/
extern void enlargeStringInfo(StringInfo str, int needed);

#define nodeTag(nodeptr) (((Node*)(nodeptr))->type)
#define makeNode(type)  ((type*)newNode(sizeof(type),T_##type))
#define nodeSetTag(nodeptr,t) (((Node*)(nodeptr))->type = (t))
#define isA(nodeptr, type)  (nodeTag(nodeptr) == T_##type)

/*extern declaration */
extern Node *newNode(size_t size, NodeTag type);

/* get a string representation of a node */
extern char *nodeToString(void *obj);

/*readfun.c*/
extern void *stringToNode(char *str);

/*copyfun.c*/
extern void *copyObject(void *obj);

/*equalfun.c*/
extern boolean equal(void *a, void *b);


#endif /*NODETYPE_H*/