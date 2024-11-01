/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-FileContributor: Suman Manjunath <msuman@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Taken from evolution-mapi and adapted/simplified to extract only exceptions (deleted and detached instances) */

#include "evolution-ews-config.h"

#include <libecal/libecal.h>

#include "e-cal-backend-m365-recur-blob.h"

/* Subset of used/recognized pattern types */
#define PatternType_Day 0x0
#define PatternType_MonthNth 0x3

/* Reader/Writer versions */
#define READER_VERSION	0x3004
#define WRITER_VERSION	0x3004
#define READER_VERSION2 0x3006
#define WRITER_VERSION2 0x3009

/* Override flags defining what fields might be found in ExceptionInfo */
#define ARO_SUBJECT 0x0001
#define ARO_MEETINGTYPE 0x0002
#define ARO_REMINDERDELTA 0x0004
#define ARO_REMINDER 0x0008
#define ARO_LOCATION 0x0010
#define ARO_BUSYSTATUS 0x0020
#define ARO_ATTACHMENT 0x0040
#define ARO_SUBTYPE 0x0080
#define ARO_APPTCOLOR 0x0100
#define ARO_EXCEPTIONAL_BODY 0x0200

/* Serialization helper: append len bytes from var to arr. */
#define GBA_APPEND(a, v, l) g_byte_array_append ((a), (guint8*)(v), (l))

/* Serialization helper: append the value of the variable to arr. */
#define GBA_APPEND_LVAL(a, v) GBA_APPEND ((a), (&v), (sizeof (v)))

/* Unserialization helper: read len bytes into buff from ba at offset off. */
#define GBA_MEMCPY_OFFSET(arr, off, buf, blen) \
	G_STMT_START { \
		g_return_val_if_fail ((off >= 0 && arr->len - off >= blen), FALSE); \
		memcpy (buf, arr->data + off, blen); \
		off += blen; \
	} G_STMT_END

/* Unserialization helper: dereference and increment pointer. */
#define GBA_DEREF_OFFSET(arr, off, lval, valtype) \
	G_STMT_START { \
		g_return_val_if_fail ((off >= 0 && arr->len - off >= sizeof (valtype)), FALSE); \
		lval = *((valtype*)(arr->data+off)); \
		off += sizeof (valtype); \
	} G_STMT_END

/** MS-OXOCAL 2.2.1.44.3 */
struct ema_ChangeHighlight {
	guint32 ChangeHighlightSize;
	guint32 ChangeHighlightValue;
	void *Reserved;
};

/** MS-OXOCAL 2.2.1.44.4 */
struct ema_ExtendedException {
	struct ema_ChangeHighlight ChangeHighlight;
	guint32 ReservedBlockEE1Size;
	void *ReservedBlockEE1;
	guint32 StartDateTime;
	guint32 EndDateTime;
	guint32 OriginalStartDate;
	guint16 WideCharSubjectLength;
	gchar *WideCharSubject;
	guint16 WideCharLocationLength;
	gchar *WideCharLocation;
	guint32 ReservedBlockEE2Size;
	void *ReservedBlockEE2;
};

/** MS-OXOCAL 2.2.1.44.2 */
struct ema_ExceptionInfo {
	guint32 StartDateTime;
	guint32 EndDateTime;
	guint32 OriginalStartDate;
	guint16 OverrideFlags;
	guint16 SubjectLength;
	guint16 SubjectLength2;
	gchar *Subject;
	guint32 MeetingType;
	guint32 ReminderDelta;
	guint32 ReminderSet;
	guint16 LocationLength;
	guint16 LocationLength2;
	gchar *Location;
	guint32 BusyStatus;
	guint32 Attachment;
	guint32 SubType;
	guint32 AppointmentColor;
};

/** MS-OXOCAL 2.2.1.44.1 */
struct ema_RecurrencePattern {
	guint16 ReaderVersion;
	guint16 WriterVersion;
	guint16 RecurFrequency;
	guint16 PatternType;
	guint16 CalendarType;
	guint32 FirstDateTime;
	guint32 Period;
	guint32 SlidingFlag;
	guint32 PatternTypeSpecific;
	guint32 N;
	guint32 EndType;
	guint32 OccurrenceCount;
	guint32 FirstDOW;
	guint32 DeletedInstanceCount;
	guint32 *DeletedInstanceDates;
	guint32 ModifiedInstanceCount;
	guint32 *ModifiedInstanceDates;
	guint32 StartDate;
	guint32 EndDate;
};

/** MS-OXOCAL 2.2.1.44.5 */
struct ema_AppointmentRecurrencePattern {
	struct ema_RecurrencePattern RecurrencePattern;
	guint32 ReaderVersion2;
	guint32 WriterVersion2;
	guint32 StartTimeOffset;
	guint32 EndTimeOffset;
	guint16 ExceptionCount;
	struct ema_ExceptionInfo *ExceptionInfo;
	guint32 ReservedBlock1Size;
	void *ReservedBlock1;
	struct ema_ExtendedException *ExtendedException;
	guint32 ReservedBlock2Size;
	void *ReservedBlock2;
};

static gboolean
gba_to_rp (const GByteArray *gba,
	   ptrdiff_t *off,
	   struct ema_RecurrencePattern *rp)
{
	GBA_DEREF_OFFSET (gba, *off, rp->ReaderVersion, guint16);
	GBA_DEREF_OFFSET (gba, *off, rp->WriterVersion, guint16);
	GBA_DEREF_OFFSET (gba, *off, rp->RecurFrequency, guint16);
	GBA_DEREF_OFFSET (gba, *off, rp->PatternType, guint16);
	GBA_DEREF_OFFSET (gba, *off, rp->CalendarType, guint16);
	GBA_DEREF_OFFSET (gba, *off, rp->FirstDateTime, guint32);
	GBA_DEREF_OFFSET (gba, *off, rp->Period, guint32);
	GBA_DEREF_OFFSET (gba, *off, rp->SlidingFlag, guint32);

	if (rp->PatternType != PatternType_Day) {
		GBA_DEREF_OFFSET (gba, *off, rp->PatternTypeSpecific, guint32);
		if (rp->PatternType == PatternType_MonthNth) {
			GBA_DEREF_OFFSET (gba, *off, rp->N,
			                  guint32);
		}
	}

	GBA_DEREF_OFFSET (gba, *off, rp->EndType, guint32);
	GBA_DEREF_OFFSET (gba, *off, rp->OccurrenceCount, guint32);
	GBA_DEREF_OFFSET (gba, *off, rp->FirstDOW, guint32);

	GBA_DEREF_OFFSET (gba, *off, rp->DeletedInstanceCount, guint32);
	if (rp->DeletedInstanceCount) {
		rp->DeletedInstanceDates = g_new (guint32,
		                                  rp->DeletedInstanceCount);
		GBA_MEMCPY_OFFSET(gba, *off, rp->DeletedInstanceDates,
		                  sizeof (guint32) * rp->DeletedInstanceCount);
	}

	GBA_DEREF_OFFSET (gba, *off, rp->ModifiedInstanceCount, guint32);
	if (rp->ModifiedInstanceCount) {
		rp->ModifiedInstanceDates = g_new (guint32,
		                                   rp->ModifiedInstanceCount);
		GBA_MEMCPY_OFFSET (gba, *off, rp->ModifiedInstanceDates,
		                   sizeof (guint32) * rp->ModifiedInstanceCount);
	}

	GBA_DEREF_OFFSET(gba, *off, rp->StartDate, guint32);
	GBA_DEREF_OFFSET(gba, *off, rp->EndDate, guint32);

	return TRUE;
}

static gboolean
gba_to_ei (const GByteArray *gba,
	   ptrdiff_t *off,
	   struct ema_ExceptionInfo *ei)
{
	GBA_DEREF_OFFSET (gba, *off, ei->StartDateTime, guint32);
	GBA_DEREF_OFFSET (gba, *off, ei->EndDateTime, guint32);
	GBA_DEREF_OFFSET (gba, *off, ei->OriginalStartDate, guint32);
	GBA_DEREF_OFFSET (gba, *off, ei->OverrideFlags, guint16);

	if (ei->OverrideFlags&ARO_SUBJECT) {
		GBA_DEREF_OFFSET (gba, *off, ei->SubjectLength, guint16);
		GBA_DEREF_OFFSET (gba, *off, ei->SubjectLength2, guint16);
		ei->Subject = g_new0 (gchar, ei->SubjectLength2 + 1);
		GBA_MEMCPY_OFFSET (gba, *off, ei->Subject, ei->SubjectLength2);
	}

	if (ei->OverrideFlags&ARO_MEETINGTYPE) {
		GBA_DEREF_OFFSET (gba, *off, ei->MeetingType, guint32);
	}

	if (ei->OverrideFlags&ARO_REMINDERDELTA) {
		GBA_DEREF_OFFSET (gba, *off, ei->ReminderDelta, guint32);
	}

	if (ei->OverrideFlags & ARO_REMINDER) {
		GBA_DEREF_OFFSET (gba, *off, ei->ReminderSet, guint32);
	}

	if (ei->OverrideFlags&ARO_LOCATION) {
		GBA_DEREF_OFFSET (gba, *off, ei->LocationLength, guint16);
		GBA_DEREF_OFFSET (gba, *off, ei->LocationLength2, guint16);
		ei->Location = g_new0 (gchar, ei->LocationLength2 + 1);
		GBA_MEMCPY_OFFSET (gba, *off, ei->Location, ei->LocationLength2);
	}

	if (ei->OverrideFlags&ARO_BUSYSTATUS) {
		GBA_DEREF_OFFSET (gba, *off, ei->BusyStatus, guint32);
	}

	if (ei->OverrideFlags&ARO_ATTACHMENT) {
		GBA_DEREF_OFFSET (gba, *off, ei->Attachment, guint32);
	}

	if (ei->OverrideFlags&ARO_SUBTYPE) {
		GBA_DEREF_OFFSET (gba, *off, ei->SubType, guint32);
	}

	if (ei->OverrideFlags&ARO_APPTCOLOR) {
		GBA_DEREF_OFFSET (gba, *off, ei->AppointmentColor, guint32);
	}

	return TRUE;
}

static gboolean
gba_to_ee (const GByteArray *gba, ptrdiff_t *off,
	   struct ema_ExtendedException *ee,
	   struct ema_AppointmentRecurrencePattern *arp, int exnum)
{
	GBA_DEREF_OFFSET (gba, *off, ee->ChangeHighlight.ChangeHighlightSize,
	                  guint32);

	if (arp->WriterVersion2 >= 0x3009) {
		if (ee->ChangeHighlight.ChangeHighlightSize > 0) {
			int reserved_size = ee->ChangeHighlight.ChangeHighlightSize - sizeof (guint32);
			GBA_DEREF_OFFSET (gba, *off,
			                  ee->ChangeHighlight.ChangeHighlightValue,
			                  guint32);
			if (reserved_size > 0) {
				ee->ChangeHighlight.Reserved = g_new (gchar, reserved_size);
				GBA_MEMCPY_OFFSET (gba, *off,
				                   &ee->ChangeHighlight.Reserved,
				                   reserved_size);
			}
		}
	}

	GBA_DEREF_OFFSET (gba, *off, ee->ReservedBlockEE1Size, guint32);
	if (ee->ReservedBlockEE1Size) {
		ee->ReservedBlockEE1 = g_new (gchar, ee->ReservedBlockEE1Size);
		GBA_MEMCPY_OFFSET (gba, *off, ee->ReservedBlockEE1,
		                   ee->ReservedBlockEE1Size);
	}

	if (arp->ExceptionInfo[exnum].OverrideFlags&(ARO_SUBJECT|ARO_LOCATION)) {
		GBA_DEREF_OFFSET (gba, *off, ee->StartDateTime, guint32);
		GBA_DEREF_OFFSET (gba, *off, ee->EndDateTime, guint32);
		GBA_DEREF_OFFSET (gba, *off, ee->OriginalStartDate, guint32);

		if(arp->ExceptionInfo[exnum].OverrideFlags&ARO_SUBJECT) {
			GBA_DEREF_OFFSET (gba, *off, ee->WideCharSubjectLength,
			                  guint16);
			ee->WideCharSubject = g_new0(gchar,
			                             sizeof(guint16) * (ee->WideCharSubjectLength + 1));
			GBA_MEMCPY_OFFSET (gba, *off, ee->WideCharSubject,
			                   sizeof(guint16) * ee->WideCharSubjectLength);
		}

		if(arp->ExceptionInfo[exnum].OverrideFlags&ARO_LOCATION) {
			GBA_DEREF_OFFSET (gba, *off, ee->WideCharLocationLength,
			                  guint16);
			ee->WideCharLocation = g_new0 (gchar,
			                               sizeof(guint16) * (ee->WideCharLocationLength + 1));
			GBA_MEMCPY_OFFSET (gba, *off, ee->WideCharLocation,
			                   sizeof (guint16) * ee->WideCharLocationLength);
		}

		GBA_DEREF_OFFSET (gba, *off, ee->ReservedBlockEE2Size, guint32);
		if (ee->ReservedBlockEE2Size) {
			ee->ReservedBlockEE2 = g_new (gchar,
			                              ee->ReservedBlockEE2Size);
			GBA_MEMCPY_OFFSET (gba, *off, ee->ReservedBlockEE2,
			                   ee->ReservedBlockEE2Size);
		}
	}

	return TRUE;
}

static gboolean
gba_to_arp (const GByteArray *gba, ptrdiff_t *off,
	    struct ema_AppointmentRecurrencePattern *arp)
{
	gint i;

	g_return_val_if_fail (gba_to_rp (gba, off, &arp->RecurrencePattern),
	                      FALSE);
	GBA_DEREF_OFFSET (gba, *off, arp->ReaderVersion2, guint32);
	GBA_DEREF_OFFSET (gba, *off, arp->WriterVersion2, guint32);
	GBA_DEREF_OFFSET (gba, *off, arp->StartTimeOffset, guint32);
	GBA_DEREF_OFFSET (gba, *off, arp->EndTimeOffset, guint32);

	GBA_DEREF_OFFSET (gba, *off, arp->ExceptionCount, guint16);
	if (arp->ExceptionCount) {
		arp->ExceptionInfo = g_new0 (struct ema_ExceptionInfo,
		                             arp->ExceptionCount);
		for (i = 0; i < arp->ExceptionCount; ++i) {
			g_return_val_if_fail (gba_to_ei (gba, off, &arp->ExceptionInfo[i]),
			                      FALSE);
		}
	}

	GBA_DEREF_OFFSET (gba, *off, arp->ReservedBlock1Size, guint32);
	if (arp->ReservedBlock1Size) {
		arp->ReservedBlock1 = g_new (gchar, arp->ReservedBlock1Size);
		GBA_MEMCPY_OFFSET (gba, *off, arp->ReservedBlock1,
		                   arp->ReservedBlock1Size);
	}

	if (arp->ExceptionCount) {
		arp->ExtendedException = g_new0 (struct ema_ExtendedException,
		                                 arp->ExceptionCount);
		for (i = 0; i < arp->ExceptionCount; ++i) {
			g_return_val_if_fail (gba_to_ee (gba, off, &arp->ExtendedException[i], arp, i),
			                      FALSE);
		}
	}

	GBA_DEREF_OFFSET (gba, *off, arp->ReservedBlock2Size, guint32);
	if (arp->ReservedBlock2Size) {
		arp->ReservedBlock2 = g_new (gchar, arp->ReservedBlock2Size);
		GBA_MEMCPY_OFFSET (gba, *off, arp->ReservedBlock2,
		                   arp->ReservedBlock2Size);
	}

	return TRUE;
}

static void
free_arp_contents (struct ema_AppointmentRecurrencePattern *arp)
{
	gint i;

	if(arp) {
		if (arp->RecurrencePattern.DeletedInstanceDates)
			g_free (arp->RecurrencePattern.DeletedInstanceDates);
		if (arp->RecurrencePattern.ModifiedInstanceDates)
			g_free (arp->RecurrencePattern.ModifiedInstanceDates);
		if (arp->ExceptionInfo) {
			for (i = 0; i < arp->RecurrencePattern.ModifiedInstanceCount; ++i) {
				if (arp->ExceptionInfo[i].Subject)
					g_free (arp->ExceptionInfo[i].Subject);
				if (arp->ExceptionInfo[i].Location)
					g_free (arp->ExceptionInfo[i].Location);
			}
			g_free (arp->ExceptionInfo);
		}
		if (arp->ReservedBlock1) {
			g_free (arp->ReservedBlock1);
		}
		if (arp->ExtendedException) {
			for (i = 0; i < arp->RecurrencePattern.ModifiedInstanceCount; ++i) {
				if (arp->ExtendedException[i].ChangeHighlight.Reserved)
					g_free (arp->ExtendedException[i].ChangeHighlight.Reserved);
				if (arp->ExtendedException[i].ReservedBlockEE1)
					g_free (arp->ExtendedException[i].ReservedBlockEE1);
				if (arp->ExtendedException[i].WideCharSubject)
					g_free (arp->ExtendedException[i].WideCharSubject);
				if (arp->ExtendedException[i].WideCharLocation)
					g_free (arp->ExtendedException[i].WideCharLocation);
				if (arp->ExtendedException[i].ReservedBlockEE2)
					g_free (arp->ExtendedException[i].ReservedBlockEE2);
			}
			g_free (arp->ExtendedException);
		}
		if (arp->ReservedBlock2) {
			g_free (arp->ReservedBlock2);
		}
	}
}

static time_t
convert_recurrence_minutes_to_timet (uint32_t minutes)
{
	const guint64 TIME_FIXUP_CONSTANT_INT = 11644473600;
	guint64 secs;

	secs = (guint64) minutes * 60;

	/* adjust by 369 years to make the secs since 1970 */
	return secs - TIME_FIXUP_CONSTANT_INT;
}

gboolean
e_cal_backend_m365_decode_recur_blob (const gchar *base64_blob,
				      ICalComponent *icomp,
				      ICalTimezone *recur_zone,
				      GSList **out_extra_detached) /* ICalComponent * */
{
	struct ema_AppointmentRecurrencePattern arp;
	struct ema_RecurrencePattern *rp; /* Convenience pointer */
	gint i;
	ptrdiff_t off = 0;
	GByteArray fake_ba;
	guchar *blob;
	gsize blob_len = 0;

	blob = g_base64_decode (base64_blob, &blob_len);
	if (blob == NULL || blob_len == 0) {
		g_free (blob);
		return FALSE;
	}

	fake_ba.data = (guint8 *) blob;
	fake_ba.len = blob_len;

	memset (&arp, 0, sizeof (struct ema_AppointmentRecurrencePattern));

	if (!gba_to_arp (&fake_ba, &off, &arp)) {
		free_arp_contents (&arp);
		g_free (blob);
		return FALSE;
	}

	rp = &arp.RecurrencePattern;

	e_cal_util_component_remove_property_by_kind (icomp, I_CAL_EXDATE_PROPERTY, TRUE);

	/* number of exceptions */
	if (rp->DeletedInstanceCount) {
		ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();

		for (i = 0; i < rp->DeletedInstanceCount; ++i) {
			ICalProperty *prop;
			ICalTime *tt;
			time_t ictime = convert_recurrence_minutes_to_timet (rp->DeletedInstanceDates[i]);

			tt = i_cal_time_new_from_timet_with_zone (ictime, 1, NULL);
			i_cal_time_set_timezone (tt, utc_zone);

			prop = i_cal_property_new_exdate (tt);
			i_cal_component_take_property (icomp, prop);
		}
	}

	/* Modified exceptions */
	if (arp.ExceptionCount && out_extra_detached) {
		ICalTime *tt;
		ECalComponentDateTime *edt;
		ECalComponentRange *rid;
		const gchar *tzid;

		*out_extra_detached = NULL;

		tzid = recur_zone ? i_cal_timezone_get_tzid (recur_zone) : "UTC";

		for (i = 0; i < arp.ExceptionCount; i++) {
			ECalComponent *detached;
			struct ema_ExceptionInfo *ei = &arp.ExceptionInfo[i];
			struct ema_ExtendedException *ee = &arp.ExtendedException[i];

			detached = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));
			if (!detached)
				continue;

			tt = i_cal_time_new_from_timet_with_zone (convert_recurrence_minutes_to_timet (ei->OriginalStartDate), 0, NULL);
			rid = e_cal_component_range_new_take (E_CAL_COMPONENT_RANGE_SINGLE, e_cal_component_datetime_new_take (tt, g_strdup (tzid)));
			e_cal_component_set_recurid (detached, rid);
			e_cal_component_range_free (rid);

			tt = i_cal_time_new_from_timet_with_zone (convert_recurrence_minutes_to_timet (ei->StartDateTime), 0, NULL);
			edt = e_cal_component_datetime_new_take (tt, g_strdup (tzid));
			e_cal_component_set_dtstart (detached, edt);
			e_cal_component_datetime_free (edt);

			tt = i_cal_time_new_from_timet_with_zone (convert_recurrence_minutes_to_timet (ei->EndDateTime), 0, NULL);
			edt = e_cal_component_datetime_new_take (tt, g_strdup (tzid));
			e_cal_component_set_dtend (detached, edt);
			e_cal_component_datetime_free (edt);

			e_cal_component_set_rdates (detached, NULL);
			e_cal_component_set_rrules (detached, NULL);
			e_cal_component_set_exdates (detached, NULL);
			e_cal_component_set_exrules (detached, NULL);

			if (ee->WideCharSubject) {
				ECalComponentText *txt;
				gchar *str;

				str = g_convert (ee->WideCharSubject,
				                 2 * ee->WideCharSubjectLength,
				                 "UTF-8", "UTF-16", NULL, NULL,
				                 NULL);
				txt = e_cal_component_text_new (str ? str : "", NULL);
				e_cal_component_set_summary (detached, txt);
				e_cal_component_text_free (txt);
				g_free (str);
			} else if (ei->Subject) {
				ECalComponentText *txt;

				txt = e_cal_component_text_new (ei->Subject, NULL);
				e_cal_component_set_summary (detached, txt);
				e_cal_component_text_free (txt);
			}

			/* Handle MeetingType */
			/* Handle ReminderDelta */
			/* Handle Reminder */

			if (ee->WideCharLocation) {
				gchar *str;

				/* LocationLength */
				str = g_convert (ee->WideCharLocation,
				                 2 * ee->WideCharLocationLength,
				                 "UTF-8", "UTF-16", NULL, NULL,
				                 NULL);
				e_cal_component_set_location (detached, str);
				g_free (str);
			} else if (ei->Location) {
				e_cal_component_set_location (detached, ei->Location);
			}

			/* Handle BusyStatus? */
			/* Handle Attachment? */
			/* Handle SubType? */
			/* Handle AppointmentColor? */
			/* do we do anything with ChangeHighlight? */

			*out_extra_detached = g_slist_prepend (*out_extra_detached, g_object_ref (e_cal_component_get_icalcomponent (detached)));

			g_clear_object (&detached);
		}

		*out_extra_detached = g_slist_reverse (*out_extra_detached);
	}

	free_arp_contents (&arp);
	g_free (blob);

	return TRUE;
}
