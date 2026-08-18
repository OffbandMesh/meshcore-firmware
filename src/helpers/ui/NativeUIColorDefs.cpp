// #822: definitions of the UIColor statics for the NATIVE TEST ENV ONLY.
//
// UIColor's members are declared in DisplayDriver.h and defined by whichever
// display driver a firmware links -- twelve drivers, each with its own palette.
// The native env links no driver, so anything that references a UIColor (now
// including the shared splash component) fails to link.
//
// This file is referenced ONLY from the [env:native] build_src_filter and is
// never compiled into firmware, where a driver already provides these. Keeping
// it out of OffbandSplash.cpp is deliberate: test scaffolding does not belong in
// a shipping translation unit.
#include "DisplayDriver.h"

ColorVal UIColor::window_bkg, UIColor::title_bkg, UIColor::title_txt;
ColorVal UIColor::primary_txt, UIColor::secondary_txt, UIColor::warning_txt;
ColorVal UIColor::popup_bkg, UIColor::popup_txt, UIColor::corp_blue;
