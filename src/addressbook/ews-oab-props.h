/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ews-oab-props.h
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _EWS_OAB_PROPS
#define _EWS_OAB_PROPS

/* Ews oab data types */
#define EWS_PTYP_INTEGER32		0x0003
#define EWS_PTYP_BOOLEAN		0x000B
#define	EWS_PTYP_OBJECT			0x000d
#define EWS_PTYP_STRING8		0x001E
#define EWS_PTYP_STRING			0x001F
#define EWS_PTYP_BINARY			0x0102
#define EWS_PTYP_MULTIPLEINTEGER32	0x1003
#define EWS_PTYP_MULTIPLESTRING8	0x101E
#define EWS_PTYP_MULTIPLESTRING		0x101F
#define EWS_PTYP_MULTIPLEBINARY		0x1102

/* Ews OAB hdr property tags */
#define EWS_PT_NAME			0x6800001F
#define EWS_PT_DN			0x6804001E
#define EWS_PT_SEQUENCE			0x68010003
#define EWS_PT_GUID			0x6802001E
#define	EWS_PT_ROOT_DEPARTMENT		0x8C98001E

/* Ews OAB address-book record property tags that we are or may be interested in */
#define EWS_PT_DISPLAY_TYPE		0x39000003
#define EWS_PT_DISPLAY_TYPE_EX		0x39050003
#define EWS_PT_EMAIL_ADDRESS		0x3003001E
#define EWS_PT_SMTP_ADDRESS		0x39FE001F
#define EWS_PT_DISPLAY_NAME		0x3001001F
#define	EWS_PT_ACCOUNT			0x3A00001F 
#define EWS_PT_SURNAME			0x3A11001F
#define EWS_PT_GIVEN_NAME		0x3A06001F
#define EWS_PT_OFFICE_LOCATION		0x3A19001F
#define EWS_PT_BUS_TEL_NUMBER		0x3A08001F
#define	EWS_PT_INITIALS			0x3A0A001F
#define EWS_PT_STREET_ADDRESS		0x3A29001F
#define EWS_PT_LOCALITY			0x3A27001F
#define EWS_PT_STATE_OR_PROVINCE	0x3A28001F
#define EWS_PT_POSTAL_CODE		0x3A2A001F
#define EWS_PT_COUNTRY			0x3A26001F
#define EWS_PT_TITLE			0x3A17001F
#define EWS_PT_COMPANY_NAME		0x3A16001F
#define EWS_PT_ASSISTANT		0x3A30001F
#define EWS_PT_DEPARTMENT_NAME		0x3A18001F
#define EWS_PT_TARGET_ADDRESS		0x8011001F 
#define EWS_PT_HOME_TEL_NUMBER		0x3A09001F
#define EWS_PT_BUS_TEL_NUMBERS		0x3A1B101F
#define	EWS_PT_HOME_TEL_NUMBERS		0x3A2F101F
#define EWS_PT_PRIMARY_FAX_NUMBER	0x3A23001F
#define EWS_PT_MOB_TEL_NUMBER		0x3A1C001F
#define EWS_PT_ASSISTANT_TEL_NUMBER	0x3A2E001F
#define EWS_PT_PAGER_NUMBER		0x3A21001F
#define EWS_PT_COMMENT			0x3004001F
#define EWS_PT_DL_MEMBER_COUNT		0x8CE20003
#define EWS_PT_DL_EXTENAL_MEMBER_COUNT	0x8CE30003
#define EWS_PT_DL_MEMBERS		0x8009101E
#define EWS_PT_MEMBER_OF_DLS		0x8008101E
#define EWS_PT_TRUNCATED_PROPS		0x68051003
#define EWS_PT_THUMBNAIL_PHOTO		0x8C9E0102

/* Extra fields seen in the wild... yes, *three* cert objects */
#define EWS_PT_DISPLAY_TYPE		0x39000003
#define EWS_PT_DISPLAY_TYPE_EX		0x39050003
#define EWS_PT_7BIT_DISPLAY_NAME	0x39ff001e
#define EWS_PT_USER_CERTIFICATE		0x3a220102
#define EWS_PT_SEND_RICH_INFO		0x3a40000b
#define EWS_PT_USER_X509_CERTIFICATE	0x3a701102
#define EWS_PT_FILE_UNDER		0x8006001e
#define EWS_PT_PROXY_ADDRESSES		0x800f101f // http://support.microsoft.com/default.aspx?scid=kb;en-us;908496&sd=rss&spid=1773
//#define EWS_PT_PHONE1_SELECTOR(WTF?)	0x806a0003 // http://www.gregthatcher.com/Scripts/VBA/Outlook/GetListOfContactsUsingPropertyAccessor.aspx

#define EWS_PT_X509_CERT		0x8c6a1102 // http://www.hradeckralove.org/file/163_1_1/
#define EWS_PT_OBJECT_TYPE		0xffe0003
//#define EWS_PT_		0x8c6d0102
//#define EWS_PT_		0x8c8e001f
//#define EWS_PT_		0x8c8f001f
//#define EWS_PT_		0x8c92001f
//#define EWS_PT_		0x8cac101f
//#define EWS_PT_		0x8cb5000b

/* EWS OAB address-book display types */
#define EWS_DT_MAILUSER			0x00000000
#define EWS_DT_DISTLIST			0x00000001
#define EWS_DT_FORUM			0x00000002
#define EWS_DT_AGENT			0x00000003
#define EWS_DT_ORGANIZATION		0x00000004
#define EWS_DT_PRIVATE_DISTLIST		0x00000005
#define EWS_DT_REMOTE_MAILUSER		0x00000006
#define EWS_DT_ROOM			0x00000007
#define EWS_DT_EQUIPMENT		0x00000008
#define EWS_DT_SEC_DISTLIST		0x00000009

#endif /* _EWS_OAB_PROPS */
