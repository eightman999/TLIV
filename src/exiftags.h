#pragma once
// EXIF / TIFF のタグ名の表（グループ別）。表に無ければ nullptr
enum ExifGroup { EG_IFD0 = 0, EG_EXIF, EG_GPS, EG_INTEROP, EG_THUMB };   // EG_THUMB（IFD1）は IFD0 と同じ表
const wchar_t* ExifTagName(ExifGroup g, int tag);
