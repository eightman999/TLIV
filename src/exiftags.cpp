#include "exiftags.h"

namespace {
struct Tag { int tag; const wchar_t* name; };

// IFD0 / IFD1
const Tag kIfd[] = {
    { 256, L"ImageWidth" }, { 257, L"ImageLength" }, { 258, L"BitsPerSample" }, { 259, L"Compression" }, { 262, L"PhotometricInterpretation" },
    { 270, L"ImageDescription" }, { 271, L"Make" }, { 272, L"Model" }, { 274, L"Orientation" }, { 277, L"SamplesPerPixel" },
    { 282, L"XResolution" }, { 283, L"YResolution" }, { 296, L"ResolutionUnit" }, { 305, L"Software" }, { 306, L"DateTime" }, { 315, L"Artist" },
    { 513, L"JPEGInterchangeFormat" }, { 514, L"JPEGInterchangeFormatLength" }, { 531, L"YCbCrPositioning" }, { 33432, L"Copyright" },
};

const Tag kExif[] = {
    { 33434, L"ExposureTime" }, { 33437, L"FNumber" }, { 34850, L"ExposureProgram" }, { 34855, L"ISOSpeedRatings" }, { 34864, L"SensitivityType" },
    { 36864, L"ExifVersion" }, { 36867, L"DateTimeOriginal" }, { 36868, L"DateTimeDigitized" },
    { 36880, L"OffsetTime" }, { 36881, L"OffsetTimeOriginal" }, { 36882, L"OffsetTimeDigitized" },
    { 37121, L"ComponentsConfiguration" }, { 37377, L"ShutterSpeedValue" }, { 37378, L"ApertureValue" }, { 37379, L"BrightnessValue" },
    { 37380, L"ExposureBiasValue" }, { 37381, L"MaxApertureValue" }, { 37382, L"SubjectDistance" }, { 37383, L"MeteringMode" },
    { 37384, L"LightSource" }, { 37385, L"Flash" }, { 37386, L"FocalLength" }, { 37500, L"MakerNote" }, { 37510, L"UserComment" },
    { 37520, L"SubSecTime" }, { 37521, L"SubSecTimeOriginal" }, { 37522, L"SubSecTimeDigitized" },
    { 40960, L"FlashpixVersion" }, { 40961, L"ColorSpace" }, { 40962, L"PixelXDimension" }, { 40963, L"PixelYDimension" },
    { 41495, L"SensingMethod" }, { 41728, L"FileSource" }, { 41729, L"SceneType" }, { 41985, L"CustomRendered" }, { 41986, L"ExposureMode" },
    { 41987, L"WhiteBalance" }, { 41988, L"DigitalZoomRatio" }, { 41989, L"FocalLengthIn35mmFilm" }, { 41990, L"SceneCaptureType" },
    { 42016, L"ImageUniqueID" }, { 42034, L"LensSpecification" }, { 42035, L"LensMake" }, { 42036, L"LensModel" },
};

const Tag kGps[] = {
    { 0, L"GPSVersionID" }, { 1, L"GPSLatitudeRef" }, { 2, L"GPSLatitude" }, { 3, L"GPSLongitudeRef" }, { 4, L"GPSLongitude" },
    { 5, L"GPSAltitudeRef" }, { 6, L"GPSAltitude" }, { 7, L"GPSTimeStamp" }, { 18, L"GPSMapDatum" }, { 29, L"GPSDateStamp" },
};

const Tag kInterop[] = { { 1, L"InteroperabilityIndex" }, { 2, L"InteroperabilityVersion" } };

template <size_t N> const wchar_t* Find(const Tag (&t)[N], int tag) {
    for (const Tag& e : t) if (e.tag == tag) return e.name;
    return nullptr;
}
}  // namespace

const wchar_t* ExifTagName(ExifGroup g, int tag) {
    switch (g) {
    case EG_IFD0: case EG_THUMB: return Find(kIfd, tag);
    case EG_EXIF: return Find(kExif, tag);
    case EG_GPS: return Find(kGps, tag);
    case EG_INTEROP: return Find(kInterop, tag);
    }
    return nullptr;
}
