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

#endif /* _EWS_OAB_PROPS */
