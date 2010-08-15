/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __MAPI_H__
#define __MAPI_H__

G_BEGIN_DECLS

typedef enum {
	MAPI_ACCESS_MODIFY            = (1 << 0),
	MAPI_ACCESS_READ              = (1 << 1),
	MAPI_ACCESS_DELETE            = (1 << 2),
	MAPI_ACCESS_CREATE_HIERARCHY  = (1 << 3),
	MAPI_ACCESS_CREATE_CONTENTS   = (1 << 4),
	MAPI_ACCESS_CREATE_ASSOCIATED = (1 << 5)
} MapiAccess;

typedef enum {
	cdoSingle    = 0,	/* non-recurring appointment */
	cdoMaster    = 1,	/* recurring appointment */
	cdoInstance  = 2,	/* single instance of recurring appointment */
	cdoException = 3	/* exception to recurring appointment */
} CdoInstanceTypes;

typedef enum {
	MAPI_STORE    = 0x1,	/* Message Store */
	MAPI_ADDRBOOK = 0x2,	/* Address Book */
	MAPI_FOLDER   = 0x3,	/* Folder */
	MAPI_ABCONT   = 0x4,	/* Address Book Container */
	MAPI_MESSAGE  = 0x5,	/* Message */
	MAPI_MAILUSER = 0x6,	/* Individual Recipient */
	MAPI_ATTACH   = 0x7,	/* Attachment */
	MAPI_DISTLIST = 0x8,	/* Distribution List Recipient */
	MAPI_PROFSECT = 0x9,	/* Profile Section */
	MAPI_STATUS   = 0xA,	/* Status Object */
	MAPI_SESSION  = 0xB,	/* Session */
	MAPI_FORMINFO = 0xC	/* Form Information */
} MapiObjectType;

typedef enum {
/*  For address book contents tables */
	DT_MAILUSER         = 0x00000000,
	DT_DISTLIST         = 0x00000001,
	DT_FORUM            = 0x00000002,
	DT_AGENT            = 0x00000003,
	DT_ORGANIZATION     = 0x00000004,
	DT_PRIVATE_DISTLIST = 0x00000005,
	DT_REMOTE_MAILUSER  = 0x00000006,
/*  For address book hierarchy tables */
	DT_MODIFIABLE       = 0x00010000,
	DT_GLOBAL           = 0x00020000,
	DT_LOCAL            = 0x00030000,
	DT_WAN              = 0x00040000,
	DT_NOT_SPECIFIC     = 0x00050000,
/*  For folder hierarchy tables */
	DT_FOLDER           = 0x01000000,
	DT_FOLDER_LINK      = 0x02000000,
	DT_FOLDER_SPECIAL   = 0x04000000
} MapiPrDisplayType;

typedef enum {
	MAPI_ORIG = 0,
	MAPI_TO   = 1,
	MAPI_CC   = 2,
	MAPI_BCC  = 3
} MapiPrRecipientType;

typedef enum {
	MAPI_MSGFLAG_READ            = 0x0001,
	MAPI_MSGFLAG_UNMODIFIED      = 0x0002,
	MAPI_MSGFLAG_SUBMIT          = 0x0004,
	MAPI_MSGFLAG_UNSENT          = 0x0008,
	MAPI_MSGFLAG_HASATTACH       = 0x0010,
	MAPI_MSGFLAG_FROMME          = 0x0020,
	MAPI_MSGFLAG_ASSOCIATED      = 0x0040,
	MAPI_MSGFLAG_RESEND          = 0x0080,
	MAPI_MSGFLAG_RN_PENDING      = 0x0100,
	MAPI_MSGFLAG_NRN_PENDING     = 0x0200,
	MAPI_MSGFLAG_ORIGIN_X400     = 0x1000,
	MAPI_MSGFLAG_ORIGIN_INTERNET = 0x2000,
	MAPI_MSGFLAG_ORIGIN_MISC_EXT = 0x8000
} MapiPrMessageFlags;

typedef enum {
	MAPI_ACTION_REPLIED   = 261,
	MAPI_ACTION_FORWARDED = 262
} MapiPrAction;

typedef enum {
	MAPI_ACTION_FLAG_REPLIED_TO_SENDER = 102,
	MAPI_ACTION_FLAG_REPLIED_TO_ALL    = 103,
	MAPI_ACTION_FLAG_FORWARDED         = 104
} MapiPrActionFlag;

typedef enum {
	MAPI_FOLLOWUP_UNFLAGGED = 0,
	MAPI_FOLLOWUP_COMPLETED = 1,
	MAPI_FOLLOWUP_FLAGGED   = 2
} MapiPrFlagStatus;

typedef enum {
	MAPI_PRIO_URGENT    =  1,
	MAPI_PRIO_NORMAL    =  0,
	MAPI_PRIO_NONURGENT = -1
} MapiPrPriority;

typedef enum {
	MAPI_SENSITIVITY_NONE                 = 0,
	MAPI_SENSITIVITY_PERSONAL             = 1,
	MAPI_SENSITIVITY_PRIVATE              = 2,
	MAPI_SENSITIVITY_COMPANY_CONFIDENTIAL = 3
} MapiPrSensitivity;

typedef enum {
	MAPI_IMPORTANCE_LOW    = 0,
	MAPI_IMPORTANCE_NORMAL = 1,
	MAPI_IMPORTANCE_HIGH   = 2
} MapiPrImportance;

G_END_DECLS

#endif /* __MAPI_H__ */
