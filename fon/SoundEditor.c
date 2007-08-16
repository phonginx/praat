/* SoundEditor.c
 *
 * Copyright (C) 1992-2007 Paul Boersma
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * pb 2002/07/16 GPL
 * pb 2003/05/21 more complete settings report
 * pb 2004/02/15 highlight selection, but not the spectrogram
 * pb 2005/06/16 units
 * pb 2005/09/21 interface update
 * pb 2006/05/10 repaired memory leak in do_write
 * pb 2006/12/30 new Sound_create API
 * pb 2007/01/27 compatible with stereo sounds
 * Erez Volk 2007/05/14 FLAC support
 * pb 2007/06/10 wchar_t
 * pb 2007/08/12 wchar_t
 */

#include "SoundEditor.h"
#include "FunctionEditor_Sound.h"
#include "FunctionEditor_SoundAnalysis.h"
#include "Sound_and_Spectrogram.h"
#include "Pitch.h"
#include "Preferences.h"
#include "EditorM.h"

#define SoundEditor_members FunctionEditor_members \
	Widget publishButton, publishPreserveButton, publishWindowButton; \
	Widget writeAiffButton, writeAifcButton, writeWavButton, writeNextSunButton, writeNistButton, writeFlacButton; \
	Widget cutButton, copyButton, pasteButton, zeroButton, reverseButton; \
	double minimum, maximum; \
	struct { int windowType; double relativeWidth; int preserveTimes; } publish; \
	double maxBuffer;
#define SoundEditor_methods FunctionEditor_methods
class_create_opaque (SoundEditor, FunctionEditor);

/********** PREFERENCES **********/

static struct {
	struct {
		int windowType;
		double relativeWidth;
		int preserveTimes;
	}
		publish;
}
	preferences = {
		{ enumi (Sound_WINDOW, Hanning), 1.0, TRUE }   /* publish */
	};

void SoundEditor_prefs (void) {
	Resources_addInt (L"SoundEditor.publish.windowType", & preferences.publish.windowType);
	Resources_addDouble (L"SoundEditor.publish.relativeWidth", & preferences.publish.relativeWidth);
	Resources_addInt (L"SoundEditor.publish.preserveTimes", & preferences.publish.preserveTimes);
}

/********** METHODS **********/

static void destroy (I) {
	iam (SoundEditor);
	FunctionEditor_SoundAnalysis_forget (me);
	inherited (SoundEditor) destroy (me);
}

static void dataChanged (I) {
	iam (SoundEditor);
	Sound sound = my data;
	Melder_assert (sound != NULL);   /* LongSound objects should not get dataChanged messages. */
	Matrix_getWindowExtrema (sound, 1, sound -> nx, 1, sound -> ny, & my minimum, & my maximum);
	FunctionEditor_SoundAnalysis_forget (me);
	inherited (SoundEditor) dataChanged (me);
}

/***** FILE MENU *****/

static int do_publish (SoundEditor me, int preserveTimes) {
	Sound publish = my longSound.data ? LongSound_extractPart (my data, my startSelection, my endSelection, preserveTimes) :
		Sound_extractPart (my data, my startSelection, my endSelection, enumi (Sound_WINDOW, Rectangular), 1.0, preserveTimes);
	if (! publish) return 0;
	if (my publishCallback)
		my publishCallback (me, my publishClosure, publish);
	return 1;
}

static int menu_cb_Publish (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	return do_publish (me, FALSE);
}

static int menu_cb_PublishPreserve (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	return do_publish (me, TRUE);
}

static int menu_cb_PublishWindow (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM ("Extract windowed selection", 0)
		WORD ("Name", "slice")
		ENUM ("Window", Sound_WINDOW, enumi (Sound_WINDOW, Hanning))
		POSITIVE ("Relative width", "1.0")
		BOOLEAN ("Preserve times", 1)
	EDITOR_OK
		SET_INTEGER ("Window", my publish.windowType)
		SET_REAL ("Relative width", my publish.relativeWidth)
		SET_INTEGER ("Preserve times", my publish.preserveTimes)
	EDITOR_DO
		Sound sound = my data, publish;
		preferences.publish.windowType = my publish.windowType = GET_INTEGER ("Window");
		preferences.publish.relativeWidth = my publish.relativeWidth = GET_REAL ("Relative width");
		preferences.publish.preserveTimes = my publish.preserveTimes = GET_INTEGER ("Preserve times");
		publish = Sound_extractPart (sound, my startSelection, my endSelection, my publish.windowType,
			my publish.relativeWidth, my publish.preserveTimes);
		if (! publish) return 0;
		Thing_setName (publish, GET_STRING ("Name"));
		if (my publishCallback)
			my publishCallback (me, my publishClosure, publish);
	EDITOR_END
}

static int do_write (SoundEditor me, MelderFile file, int format) {
	if (my startSelection >= my endSelection)
		return Melder_error ("No samples selected.");
	if (my longSound.data) {
		return LongSound_writePartToAudioFile16 (my data, format, my startSelection, my endSelection, file);
	} else {
		Sound sound = my data;
		double margin = 0.0;
		long nmargin = margin / sound -> dx;
		long first, last, numberOfSamples = Sampled_getWindowSamples (sound,
			my startSelection, my endSelection, & first, & last) + nmargin * 2;
		first -= nmargin;
		last += nmargin;
		if (numberOfSamples) {
			Sound save = Sound_create (sound -> ny, 0.0, numberOfSamples * sound -> dx,
							numberOfSamples, sound -> dx, 0.5 * sound -> dx);
			if (! save) return 0;
			long offset = first - 1;
			if (first < 1) first = 1;
			if (last > sound -> nx) last = sound -> nx;
			for (long channel = 1; channel <= sound -> ny; channel ++) {
				for (long i = first; i <= last; i ++) {
					save -> z [channel] [i - offset] = sound -> z [channel] [i];
				}
			}
			int result = Sound_writeToAudioFile16 (save, file, format);
			forget (save);
			return result;
		}
	}
	return 0;
}

static int menu_cb_WriteWav (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to WAV file", 0)
		swprintf (defaultName, 300, L"%ls.wav", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_WAV)) return 0;
	EDITOR_END
}

static int menu_cb_WriteAiff (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to AIFF file", 0)
		swprintf (defaultName, 300, L"%ls.aiff", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_AIFF)) return 0;
	EDITOR_END
}

static int menu_cb_WriteAifc (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to AIFC file", 0)
		swprintf (defaultName, 300, L"%ls.aifc", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_AIFC)) return 0;
	EDITOR_END
}

static int menu_cb_WriteNextSun (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to NeXT/Sun file", 0)
		swprintf (defaultName, 300, L"%ls.au", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_NEXT_SUN)) return 0;
	EDITOR_END
}

static int menu_cb_WriteNist (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to NIST file", 0)
		swprintf (defaultName, 300, L"%ls.nist", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_NIST)) return 0;
	EDITOR_END
}

static int menu_cb_WriteFlac (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	EDITOR_FORM_WRITE (L"Write selection to FLAC file", 0)
		swprintf (defaultName, 300, L"%ls.flac", my longSound.data ? my longSound.data -> nameW : my sound.data -> nameW);
	EDITOR_DO_WRITE
		if (! do_write (me, file, Melder_FLAC)) return 0;
	EDITOR_END
}

/***** EDIT MENU *****/

static int menu_cb_Copy (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	Sound publish = my longSound.data ? LongSound_extractPart (my data, my startSelection, my endSelection, FALSE) :
		Sound_extractPart (my data, my startSelection, my endSelection, enumi (Sound_WINDOW, Rectangular), 1.0, FALSE);
	iferror return 0;
	forget (Sound_clipboard);
	Sound_clipboard = publish;
	return 1;
}

static int menu_cb_Cut (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	Sound sound = my data;
	long first, last, selectionNumberOfSamples = Sampled_getWindowSamples (sound,
		my startSelection, my endSelection, & first, & last);
	long oldNumberOfSamples = sound -> nx;
	long newNumberOfSamples = oldNumberOfSamples - selectionNumberOfSamples;
	if (newNumberOfSamples < 1)
		return Melder_error ("(SoundEditor_cut:) You cannot cut all of the signal away,\n"
			"because you cannot create a Sound with 0 samples.\n"
			"You could consider using Copy instead.");
	if (selectionNumberOfSamples) {
		float **newData, **oldData = sound -> z;
		forget (Sound_clipboard);
		Sound_clipboard = Sound_create (sound -> ny, 0.0, selectionNumberOfSamples * sound -> dx,
						selectionNumberOfSamples, sound -> dx, 0.5 * sound -> dx);
		if (! Sound_clipboard) return 0;
		for (long channel = 1; channel <= sound -> ny; channel ++) {
			long j = 0;
			for (long i = first; i <= last; i ++) {
				Sound_clipboard -> z [channel] [++ j] = oldData [channel] [i];
			}
		}
		newData = NUMfmatrix (1, sound -> ny, 1, newNumberOfSamples);
		for (long channel = 1; channel <= sound -> ny; channel ++) {
			long j = 0;
			for (long i = 1; i < first; i ++) {
				newData [channel] [++ j] = oldData [channel] [i];
			}
			for (long i = last + 1; i <= oldNumberOfSamples; i ++) {
				newData [channel] [++ j] = oldData [channel] [i];
			}
		}
		Editor_save (me, L"Cut");
		NUMfmatrix_free (oldData, 1, 1);
		sound -> xmin = 0.0;
		sound -> xmax = newNumberOfSamples * sound -> dx;
		sound -> nx = newNumberOfSamples;
		sound -> x1 = 0.5 * sound -> dx;
		sound -> z = newData;

		/* Start updating the markers of the FunctionEditor, respecting the invariants. */

		my tmin = sound -> xmin;
		my tmax = sound -> xmax;

		/* Collapse the selection, */
		/* so that the Cut operation can immediately be undone by a Paste. */
		/* The exact position will be half-way in between two samples. */

		my startSelection = my endSelection = sound -> xmin + (first - 1) * sound -> dx;

		/* Update the window. */
		{
			double t1 = (first - 1) * sound -> dx;
			double t2 = last * sound -> dx;
			double windowLength = my endWindow - my startWindow;   /* > 0 */
			if (t1 > my startWindow)
				if (t2 < my endWindow)
					my startWindow -= 0.5 * (t2 - t1);
				else
					(void) 0;
			else if (t2 < my endWindow)
				my startWindow -= t2 - t1;
			else   /* Cut overlaps entire window: centre. */
				my startWindow = my startSelection - 0.5 * windowLength;
			my endWindow = my startWindow + windowLength;   /* First try. */
			if (my endWindow > my tmax) {
				my startWindow -= my endWindow - my tmax;   /* 2nd try. */
				if (my startWindow < my tmin)
					my startWindow = my tmin;   /* Third try. */
				my endWindow = my tmax;   /* Second try. */
			} else if (my startWindow < my tmin) {
				my endWindow -= my startWindow - my tmin;   /* Second try. */
				if (my endWindow > my tmax)
					my endWindow = my tmax;   /* Third try. */
				my startWindow = my tmin;   /* Second try. */
			}
		}

		/* Force FunctionEditor to show changes. */

		Matrix_getWindowExtrema (sound, 1, sound -> nx, 1, sound -> ny, & my minimum, & my maximum);
		FunctionEditor_SoundAnalysis_forget (me);
		FunctionEditor_ungroup (me);
		FunctionEditor_marksChanged (me);
		Editor_broadcastChange (me);
	} else {
		Melder_warning ("No samples selected.");
	}
	return 1;
}

static int menu_cb_Paste (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	Sound sound = my data;
	long leftSample = Sampled_xToLowIndex (sound, my endSelection);
	long oldNumberOfSamples = sound -> nx, newNumberOfSamples;
	float **newData, **oldData = sound -> z;
	if (! Sound_clipboard) {
		Melder_warning ("(SoundEditor_paste:) Clipboard is empty; nothing pasted.");
		return 1;
	}
	if (Sound_clipboard -> ny != sound -> ny)
		return Melder_error ("(SoundEditor_paste:) Cannot paste because\n"
 			"number of channels of clipboard does not match\n"
			"number of channels of edited sound.");
	if (Sound_clipboard -> dx != sound -> dx)
		return Melder_error ("(SoundEditor_paste:) Cannot paste because\n"
 			"sampling frequency of clipboard does not match\n"
			"sampling frequency of edited sound.");
	if (leftSample < 0) leftSample = 0;
	if (leftSample > oldNumberOfSamples) leftSample = oldNumberOfSamples;
	newNumberOfSamples = oldNumberOfSamples + Sound_clipboard -> nx;
	if (! (newData = NUMfmatrix (1, sound -> ny, 1, newNumberOfSamples))) return 0;
	for (long channel = 1; channel <= sound -> ny; channel ++) {
		long j = 0;
		for (long i = 1; i <= leftSample; i ++) {
			newData [channel] [++ j] = oldData [channel] [i];
		}
		for (long i = 1; i <= Sound_clipboard -> nx; i ++) {
			newData [channel] [++ j] = Sound_clipboard -> z [channel] [i];
		}
		for (long i = leftSample + 1; i <= oldNumberOfSamples; i ++) {
			newData [channel] [++ j] = oldData [channel] [i];
		}
	}
	Editor_save (me, L"Paste");
	NUMfmatrix_free (oldData, 1, 1);
	sound -> xmin = 0.0;
	sound -> xmax = newNumberOfSamples * sound -> dx;
	sound -> nx = newNumberOfSamples;
	sound -> x1 = 0.5 * sound -> dx;
	sound -> z = newData;

	/* Start updating the markers of the FunctionEditor, respecting the invariants. */

	my tmin = sound -> xmin;
 	my tmax = sound -> xmax;
	my startSelection = leftSample * sound -> dx;
	my endSelection = (leftSample + Sound_clipboard -> nx) * sound -> dx;

	/* Force FunctionEditor to show changes. */

	Matrix_getWindowExtrema (sound, 1, sound -> nx, 1, sound -> ny, & my minimum, & my maximum);
	FunctionEditor_SoundAnalysis_forget (me);
	FunctionEditor_ungroup (me);
	FunctionEditor_marksChanged (me);
	Editor_broadcastChange (me);
	return 1;
}

static int menu_cb_SetSelectionToZero (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	Sound sound = my data;
	long first, last;
	Sampled_getWindowSamples (sound, my startSelection, my endSelection, & first, & last);
	Editor_save (me, L"Set to zero");
	for (long channel = 1; channel <= sound -> ny; channel ++) {
		for (long i = first; i <= last; i ++) {
			sound -> z [channel] [i] = 0.0;
		}
	}
	FunctionEditor_SoundAnalysis_forget (me);
	FunctionEditor_redraw (me);
	Editor_broadcastChange (me);
	return 1;
}

static int menu_cb_ReverseSelection (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	Editor_save (me, L"Reverse selection");
	Sound_reverse (my data, my startSelection, my endSelection);
	FunctionEditor_SoundAnalysis_forget (me);
	FunctionEditor_redraw (me);
	Editor_broadcastChange (me);
	return 1;
}

/***** QUERY MENU *****/

static int menu_cb_SettingsReport (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	MelderInfo_open ();
	MelderInfo_writeLine2 (L"Data class: ", ((Thing) my data) -> methods -> _classNameW);
	MelderInfo_writeLine2 (L"Data name: ", ((Thing) my data) -> nameW);
	MelderInfo_writeLine3 (L"Editor start: ", Melder_double (my tmin), L" seconds");
	MelderInfo_writeLine3 (L"Editor end: ", Melder_double (my tmax), L" seconds");
	MelderInfo_writeLine3 (L"Window start: ", Melder_double (my startWindow), L" seconds");
	MelderInfo_writeLine3 (L"Window end: ", Melder_double (my endWindow), L" seconds");
	MelderInfo_writeLine3 (L"Selection start: ", Melder_double (my startSelection), L" seconds");
	MelderInfo_writeLine3 (L"Selection end: ", Melder_double (my endSelection), L" seconds");
	/* Sound flag: */
	MelderInfo_writeLine2 (L"Sound autoscaling: ", Melder_boolean (my sound.autoscaling));
	/* Spectrogram flag: */
	MelderInfo_writeLine2 (L"Spectrogram show: ", Melder_boolean (my spectrogram.show));
	/* Spectrogram settings: */
	MelderInfo_writeLine3 (L"Spectrogram view from: ", Melder_double (my spectrogram.viewFrom), L" Hertz");
	MelderInfo_writeLine3 (L"Spectrogram view to: ", Melder_double (my spectrogram.viewTo), L" Hertz");
	MelderInfo_writeLine3 (L"Spectrogram window length: ", Melder_double (my spectrogram.windowLength), L" seconds");
	MelderInfo_writeLine3 (L"Spectrogram dynamic range: ", Melder_double (my spectrogram.dynamicRange), L" dB");
	/* Advanced spectrogram settings: */
	MelderInfo_writeLine2 (L"Spectrogram number of time steps: ", Melder_integer (my spectrogram.timeSteps));
	MelderInfo_writeLine2 (L"Spectrogram number of frequency steps: ", Melder_integer (my spectrogram.frequencySteps));
	MelderInfo_writeLine2 (L"Spectrogram method: ", L"Fourier");
	MelderInfo_writeLine2 (L"Spectrogram window shape: ", Sound_to_Spectrogram_windowShapeText (my spectrogram.windowShape));
	MelderInfo_writeLine2 (L"Spectrogram autoscaling: ", Melder_boolean (my spectrogram.autoscaling));
	MelderInfo_writeLine3 (L"Spectrogram maximum: ", Melder_double (my spectrogram.maximum), L" dB/Hz");
	MelderInfo_writeLine3 (L"Spectrogram pre-emphasis: ", Melder_integer (my spectrogram.preemphasis), L" dB/octave");
	MelderInfo_writeLine2 (L"Spectrogram dynamicCompression: ", Melder_integer (my spectrogram.dynamicCompression));
	/* Dynamic information: */
	MelderInfo_writeLine3 (L"Spectrogram cursor frequency: ", Melder_double (my spectrogram.cursor), L" Hertz");
	/* Pitch flag: */
	MelderInfo_writeLine2 (L"Pitch show: ", Melder_boolean (my pitch.show));
	/* Pitch settings: */
	MelderInfo_writeLine3 (L"Pitch floor: ", Melder_double (my pitch.floor), L" Hertz");
	MelderInfo_writeLine3 (L"Pitch ceiling: ", Melder_double (my pitch.ceiling), L" Hertz");
	MelderInfo_writeLine2 (L"Pitch unit: ", ClassFunction_getUnitText (classPitch, Pitch_LEVEL_FREQUENCY, my pitch.unit, Function_UNIT_TEXT_MENU));
	/* Advanced pitch settings: */
	MelderInfo_writeLine4 (L"Pitch view from: ", Melder_double (my pitch.viewFrom), L" ", ClassFunction_getUnitText (classPitch, Pitch_LEVEL_FREQUENCY, my pitch.unit, Function_UNIT_TEXT_MENU));
	MelderInfo_writeLine4 (L"Pitch view to: ", Melder_double (my pitch.viewTo), L" ", ClassFunction_getUnitText (classPitch, Pitch_LEVEL_FREQUENCY, my pitch.unit, Function_UNIT_TEXT_MENU));
	MelderInfo_writeLine2 (L"Pitch method: ", my pitch.method == 1 ? L"Autocorrelation" : L"Forward cross-correlation");
	MelderInfo_writeLine2 (L"Pitch very accurate: ", Melder_boolean (my pitch.veryAccurate));
	MelderInfo_writeLine2 (L"Pitch max. number of candidates: ", Melder_integer (my pitch.maximumNumberOfCandidates));
	MelderInfo_writeLine3 (L"Pitch silence threshold: ", Melder_double (my pitch.silenceThreshold), L" of global peak");
	MelderInfo_writeLine3 (L"Pitch voicing threshold: ", Melder_double (my pitch.voicingThreshold), L" (periodic power / total power)");
	MelderInfo_writeLine3 (L"Pitch octave cost: ", Melder_double (my pitch.octaveCost), L" per octave");
	MelderInfo_writeLine3 (L"Pitch octave jump cost: ", Melder_double (my pitch.octaveJumpCost), L" per octave");
	MelderInfo_writeLine3 (L"Pitch voiced/unvoiced cost: ", Melder_double (my pitch.voicedUnvoicedCost), L" Hertz");
	/* Intensity flag: */
	MelderInfo_writeLine2 (L"Intensity show: ", Melder_boolean (my intensity.show));
	/* Intensity settings: */
	MelderInfo_writeLine3 (L"Intensity view from: ", Melder_double (my intensity.viewFrom), L" dB");
	MelderInfo_writeLine3 (L"Intensity view to: ", Melder_double (my intensity.viewTo), L" dB");
	/* Formant flag: */
	MelderInfo_writeLine2 (L"Formant show: ", Melder_boolean (my formant.show));
	/* Formant settings: */
	MelderInfo_writeLine3 (L"Formant maximum formant: ", Melder_double (my formant.maximumFormant), L" Hertz");
	MelderInfo_writeLine2 (L"Formant number of poles: ", Melder_integer (my formant.numberOfPoles));
	MelderInfo_writeLine3 (L"Formant window length: ", Melder_double (my formant.windowLength), L" seconds");
	MelderInfo_writeLine3 (L"Formant dynamic range: ", Melder_double (my formant.dynamicRange), L" dB");
	MelderInfo_writeLine3 (L"Formant dot size: ", Melder_double (my formant.dotSize), L" mm");
	/* Advanced formant settings: */
	MelderInfo_writeLine2 (L"Formant method: ", L"Burg");
	MelderInfo_writeLine3 (L"Formant pre-emphasis from: ", Melder_double (my formant.preemphasisFrom), L" Hertz");
	/* Pulses flag: */
	MelderInfo_writeLine2 (L"Pulses show: ", Melder_boolean (my pulses.show));
	MelderInfo_close ();
	return 1;
}

/***** SELECT MENU *****/

static int menu_cb_MoveCursorToZero (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	double zero = Sound_getNearestZeroCrossing (my data, 0.5 * (my startSelection + my endSelection), 1);   // STEREO BUG
	if (NUMdefined (zero)) {
		my startSelection = my endSelection = zero;
		FunctionEditor_marksChanged (me);
	}
	return 1;
}

static int menu_cb_MoveBtoZero (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	double zero = Sound_getNearestZeroCrossing (my data, my startSelection, 1);   // STEREO BUG
	if (NUMdefined (zero)) {
		my startSelection = zero;
		if (my startSelection > my endSelection) {
			double dummy = my startSelection;
			my startSelection = my endSelection;
			my endSelection = dummy;
		}
		FunctionEditor_marksChanged (me);
	}
	return 1;
}

static int menu_cb_MoveEtoZero (EDITOR_ARGS) {
	EDITOR_IAM (SoundEditor);
	double zero = Sound_getNearestZeroCrossing (my data, my endSelection, 1);   // STEREO BUG
	if (NUMdefined (zero)) {
		my endSelection = zero;
		if (my startSelection > my endSelection) {
			double dummy = my startSelection;
			my startSelection = my endSelection;
			my endSelection = dummy;
		}
		FunctionEditor_marksChanged (me);
	}
	return 1;
}

/***** HELP MENU *****/

static int menu_cb_SoundEditorHelp (EDITOR_ARGS) { EDITOR_IAM (SoundEditor); Melder_help (L"SoundEditor"); return 1; }
static int menu_cb_LongSoundEditorHelp (EDITOR_ARGS) { EDITOR_IAM (SoundEditor); Melder_help (L"LongSoundEditor"); return 1; }

static void createMenus (I) {
	iam (SoundEditor);
	inherited (SoundEditor) createMenus (me);

	Editor_addCommand (me, L"File", L"Copy to list of objects:", motif_INSENSITIVE, menu_cb_Publish /* dummy */);
	my publishPreserveButton = Editor_addCommand (me, L"File", L"Extract sound selection (preserve times)", 0, menu_cb_PublishPreserve);
	Editor_addCommand (me, L"File", L"Extract selection (preserve times)", Editor_HIDDEN, menu_cb_PublishPreserve);
	my publishButton = Editor_addCommand (me, L"File", L"Extract sound selection (time from 0)", 0, menu_cb_Publish);
	Editor_addCommand (me, L"File", L"Extract selection (time from 0)", Editor_HIDDEN, menu_cb_Publish);
	Editor_addCommand (me, L"File", L"Extract selection", Editor_HIDDEN, menu_cb_Publish);
	if (my sound.data) {
		my publishWindowButton = Editor_addCommand (me, L"File", L"Extract windowed sound selection...", 0, menu_cb_PublishWindow);
		Editor_addCommand (me, L"File", L"Extract windowed selection...", Editor_HIDDEN, menu_cb_PublishWindow);
	}
	Editor_addCommand (me, L"File", L"-- write --", 0, NULL);
	Editor_addCommand (me, L"File", L"Copy to disk:", motif_INSENSITIVE, menu_cb_Publish /* dummy */);
	my writeWavButton = Editor_addCommand (me, L"File", L"Write sound selection to WAV file...", 0, menu_cb_WriteWav);
	Editor_addCommand (me, L"File", L"Write selection to WAV file...", Editor_HIDDEN, menu_cb_WriteWav);
	my writeAiffButton = Editor_addCommand (me, L"File", L"Write sound selection to AIFF file...", 0, menu_cb_WriteAiff);
	Editor_addCommand (me, L"File", L"Write selection to AIFF file...", Editor_HIDDEN, menu_cb_WriteAiff);
	my writeAifcButton = Editor_addCommand (me, L"File", L"Write sound selection to AIFC file...", 0, menu_cb_WriteAifc);
	Editor_addCommand (me, L"File", L"Write selection to AIFC file...", Editor_HIDDEN, menu_cb_WriteAifc);
	my writeNextSunButton = Editor_addCommand (me, L"File", L"Write sound selection to Next/Sun file...", 0, menu_cb_WriteNextSun);
	Editor_addCommand (me, L"File", L"Write selection to Next/Sun file...", Editor_HIDDEN, menu_cb_WriteNextSun);
	my writeNistButton = Editor_addCommand (me, L"File", L"Write sound selection to NIST file...", 0, menu_cb_WriteNist);
	my writeFlacButton = Editor_addCommand (me, L"File", L"Write sound selection to FLAC file...", 0, menu_cb_WriteFlac);
	Editor_addCommand (me, L"File", L"Write selection to NIST file...", Editor_HIDDEN, menu_cb_WriteNist);
	Editor_addCommand (me, L"File", L"-- close --", 0, NULL);

	Editor_addCommand (me, L"Edit", L"-- cut copy paste --", 0, NULL);
	if (my sound.data) my cutButton = Editor_addCommand (me, L"Edit", L"Cut", 'X', menu_cb_Cut);
	my copyButton = Editor_addCommand (me, L"Edit", L"Copy selection to Sound clipboard", 'C', menu_cb_Copy);
	if (my sound.data) my pasteButton = Editor_addCommand (me, L"Edit", L"Paste after selection", 'V', menu_cb_Paste);
	if (my sound.data) {
		Editor_addCommand (me, L"Edit", L"-- zero --", 0, NULL);
		my zeroButton = Editor_addCommand (me, L"Edit", L"Set selection to zero", 0, menu_cb_SetSelectionToZero);
		my reverseButton = Editor_addCommand (me, L"Edit", L"Reverse selection", 'R', menu_cb_ReverseSelection);
	}

	FunctionEditor_SoundAnalysis_selectionQueries (me);

	if (my sound.data) {
		Editor_addCommand (me, L"Select", L"-- move to zero --", 0, 0);
		Editor_addCommand (me, L"Select", L"Move start of selection to nearest zero crossing", ',', menu_cb_MoveBtoZero);
		Editor_addCommand (me, L"Select", L"Move begin of selection to nearest zero crossing", Editor_HIDDEN, menu_cb_MoveBtoZero);
		Editor_addCommand (me, L"Select", L"Move cursor to nearest zero crossing", '0', menu_cb_MoveCursorToZero);
		Editor_addCommand (me, L"Select", L"Move end of selection to nearest zero crossing", '.', menu_cb_MoveEtoZero);
	}

	FunctionEditor_SoundAnalysis_addMenus (me);
	Editor_addCommand (me, L"Query", L"-- reports --", 0, 0);
	Editor_addCommand (me, L"Query", L"Settings report", 0, menu_cb_SettingsReport);

	Editor_addCommand (me, L"Help", L"SoundEditor help", '?', menu_cb_SoundEditorHelp);
	Editor_addCommand (me, L"Help", L"LongSoundEditor help", 0, menu_cb_LongSoundEditorHelp);
}

/********** UPDATE **********/

static void prepareDraw (I) {
	iam (SoundEditor);
	if (my longSound.data) {
		LongSound_haveWindow (my longSound.data, my startWindow, my endWindow);
		Melder_clearError ();
	}
}

static void draw (I) {
	iam (SoundEditor);
	long first, last, selectedSamples;
	Graphics_Viewport viewport;
	int showAnalysis = my spectrogram.show || my pitch.show || my intensity.show || my formant.show;

	/*
	 * We check beforehand whether the window fits the LongSound buffer.
	 */
	if (my longSound.data && my endWindow - my startWindow > my longSound.data -> bufferLength) {
		Graphics_setColour (my graphics, Graphics_WHITE);
		Graphics_setWindow (my graphics, 0, 1, 0, 1);
		Graphics_fillRectangle (my graphics, 0, 1, 0, 1);
		Graphics_setColour (my graphics, Graphics_BLACK);
		Graphics_setTextAlignment (my graphics, Graphics_CENTRE, Graphics_BOTTOM);
		Graphics_printf (my graphics, 0.5, 0.5, L"(window longer than %.7g seconds)", my longSound.data -> bufferLength);
		Graphics_setTextAlignment (my graphics, Graphics_CENTRE, Graphics_TOP);
		Graphics_printf (my graphics, 0.5, 0.5, L"(zoom in to see the samples)");
		return;
	}

	/* Draw sound. */

	if (showAnalysis)
		viewport = Graphics_insetViewport (my graphics, 0, 1, 0.5, 1);
	Graphics_setColour (my graphics, Graphics_WHITE);
	Graphics_setWindow (my graphics, 0, 1, 0, 1);
	Graphics_fillRectangle (my graphics, 0, 1, 0, 1);
	FunctionEditor_Sound_draw (me, my minimum, my maximum);
	Graphics_flushWs (my graphics);
	if (showAnalysis)
		Graphics_resetViewport (my graphics, viewport);

	/* Draw analyses. */

	if (showAnalysis) {
		/* Draw spectrogram, pitch, formants. */
		viewport = Graphics_insetViewport (my graphics, 0, 1, 0, 0.5);
		FunctionEditor_SoundAnalysis_draw (me);
		Graphics_flushWs (my graphics);
		Graphics_resetViewport (my graphics, viewport);
	}

	/* Draw pulses. */

	if (my pulses.show) {
		if (showAnalysis)
			viewport = Graphics_insetViewport (my graphics, 0, 1, 0.5, 1);
		FunctionEditor_SoundAnalysis_drawPulses (me);
		FunctionEditor_Sound_draw (me, my minimum, my maximum);   /* Second time, partially across the pulses. */
		Graphics_flushWs (my graphics);
		if (showAnalysis)
			Graphics_resetViewport (my graphics, viewport);
	}

	/* Update buttons. */

	selectedSamples = Sampled_getWindowSamples (my data, my startSelection, my endSelection, & first, & last);
	XtSetSensitive (my publishButton, selectedSamples != 0);
	XtSetSensitive (my publishPreserveButton, selectedSamples != 0);
	if (my publishWindowButton) XtSetSensitive (my publishWindowButton, selectedSamples != 0);
	XtSetSensitive (my writeAiffButton, selectedSamples != 0);
	XtSetSensitive (my writeAifcButton, selectedSamples != 0);
	XtSetSensitive (my writeWavButton, selectedSamples != 0);
	XtSetSensitive (my writeNextSunButton, selectedSamples != 0);
	XtSetSensitive (my writeNistButton, selectedSamples != 0);
	XtSetSensitive (my writeFlacButton, selectedSamples != 0);
	if (my sound.data) {
		XtSetSensitive (my cutButton, selectedSamples != 0 && selectedSamples < my sound.data -> nx);
		XtSetSensitive (my copyButton, selectedSamples != 0);
		XtSetSensitive (my zeroButton, selectedSamples != 0);
		XtSetSensitive (my reverseButton, selectedSamples != 0);
	}
}

static void play (I, double tmin, double tmax) {
	iam (SoundEditor);
	if (my longSound.data)
		LongSound_playPart (my data, tmin, tmax, our playCallback, me);
	else
		Sound_playPart (my data, tmin, tmax, our playCallback, me);
}

static int click (I, double xWC, double yWC, int shiftKeyPressed) {
	iam (SoundEditor);
	if ((my spectrogram.show || my formant.show) && yWC < 0.5) {
		my spectrogram.cursor = my spectrogram.viewFrom +
			2 * yWC * (my spectrogram.viewTo - my spectrogram.viewFrom);
	}
	return inherited (SoundEditor) click (me, xWC, yWC, shiftKeyPressed);   /* Drag & update. */
}

static void viewMenuEntries (I) {
	iam (SoundEditor);
	FunctionEditor_Sound_createMenus (me);
	FunctionEditor_SoundAnalysis_viewMenus (me);
}

static void highlightSelection (I, double left, double right, double bottom, double top) {
	iam (SoundEditor);
	if (my spectrogram.show)
		Graphics_highlight (my graphics, left, right, 0.5 * (bottom + top), top);
	else
		Graphics_highlight (my graphics, left, right, bottom, top);
}

static void unhighlightSelection (I, double left, double right, double bottom, double top) {
	iam (SoundEditor);
	if (my spectrogram.show)
		Graphics_unhighlight (my graphics, left, right, 0.5 * (bottom + top), top);
	else
		Graphics_unhighlight (my graphics, left, right, bottom, top);
}

class_methods (SoundEditor, FunctionEditor) {
	class_method (destroy)
	class_method (createMenus)
	class_method (dataChanged)
	class_method (prepareDraw)
	class_method (draw)
	class_method (play)
	class_method (click)
	class_method (viewMenuEntries)
	class_method (highlightSelection)
	class_method (unhighlightSelection)
	class_methods_end
}

SoundEditor SoundEditor_create (Widget parent, const wchar_t *title, Any data) {
	SoundEditor me = new (SoundEditor);
	if (Thing_member (data, classLongSound))
		my longSound.data = data;
	else if (Thing_member (data, classSound))
		my sound.data = data;
	if (! me || ! FunctionEditor_init (me, parent, title, data))
		return NULL;
	if (my longSound.data)
		my minimum = -1, my maximum = 1;
	else
		Matrix_getWindowExtrema (data, 1, my sound.data -> nx, 1, my sound.data -> ny, & my minimum, & my maximum);
	FunctionEditor_Sound_init (me);
	FunctionEditor_SoundAnalysis_init (me);
	my publish.windowType = preferences.publish.windowType;
	my publish.relativeWidth = preferences.publish.relativeWidth;
	my publish.preserveTimes = preferences.publish.preserveTimes;
	if (my longSound.data && my endWindow - my startWindow > 30.0) {
		my endWindow = my startWindow + 30.0;
		FunctionEditor_marksChanged (me);
	}
	return me;
}

/* End of file SoundEditor.c */
