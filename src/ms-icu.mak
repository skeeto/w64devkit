# C++ API excluded intentionally: incompatible with VC++
# TODO: generate icudt78_dat
# TODO: generate icu.h
# TODO: possibly more exports?
# TODO: Microsoft patching

src = $(wildcard common/*.cpp)
obj = $(patsubst %.cpp,%.cpp.o,$(src))

all: icu.dll icu.dll.a

icu.dll: $(obj) icu.def
	@echo LD $@
	@c++ -shared -s -Wl,--defsym=icudt78_dat=0 -o $@ $^

icu.dll.a: icu.def
	@echo DLLTOOL $@
	dlltool -d $^ -l $@

%.cpp.o: %.cpp
	@echo C++ $@
	@c++ -c -w -O2 -Icommon -DU_COMMON_IMPLEMENTATION \
	    -DU_EXPORT= -DU_DISABLE_RENAMING -o $@ $<

clean:
	rm -f icu.dll icu.lib icu.def $(obj)

.ONESHELL:
icu.def:
	@echo GEN $@
	@cat >$@ <<EOF
	LIBRARY icu
	EXPORTS
	u_UCharsToChars
	u_asciiToUpper
	u_austrcpy
	u_austrncpy
	u_caseInsensitivePrefixMatch
	u_catclose
	u_catgets
	u_catopen
	u_charAge
	u_charDigitValue
	u_charDirection
	u_charFromName
	u_charMirror
	u_charName
	u_charType
	u_charsToUChars
	u_cleanup
	u_countChar32
	u_digit
	u_enumCharNames
	u_enumCharTypes
	u_errorName
	u_flushDefaultConverter
	u_foldCase
	u_forDigit
	u_getBidiPairedBracket
	u_getBinaryPropertySet
	u_getCombiningClass
	u_getDataDirectory
	u_getDataVersion
	u_getDefaultConverter
	u_getFC_NFKC_Closure
	u_getIDTypes
	u_getISOComment
	u_getIntPropertyMap
	u_getIntPropertyMaxValue
	u_getIntPropertyMinValue
	u_getIntPropertyValue
	u_getNumericValue
	u_getPropertyEnum
	u_getPropertyName
	u_getPropertyValueEnum
	u_getPropertyValueName
	u_getTimeZoneFilesDirectory
	u_getUnicodeVersion
	u_getVersion
	u_hasBinaryProperty
	u_hasIDType
	u_init
	u_isIDIgnorable
	u_isIDPart
	u_isIDStart
	u_isISOControl
	u_isJavaIDPart
	u_isJavaIDStart
	u_isJavaSpaceChar
	u_isMirrored
	u_isUAlphabetic
	u_isULowercase
	u_isUUppercase
	u_isUWhiteSpace
	u_isWhitespace
	u_isalnum
	u_isalpha
	u_isbase
	u_isblank
	u_iscntrl
	u_isdefined
	u_isdigit
	u_isgraph
	u_islower
	u_isprint
	u_ispunct
	u_isspace
	u_istitle
	u_isupper
	u_isxdigit
	u_memcasecmp
	u_memchr
	u_memchr32
	u_memcmp
	u_memcmpCodePointOrder
	u_memcpy
	u_memmove
	u_memrchr
	u_memrchr32
	u_memset
	u_releaseDefaultConverter
	u_setAtomicIncDecFunctions
	u_setDataDirectory
	u_setMemoryFunctions
	u_setMutexFunctions
	u_setTimeZoneFilesDirectory
	u_shapeArabic
	u_strCaseCompare
	u_strCompare
	u_strCompareIter
	u_strFindFirst
	u_strFindLast
	u_strFoldCase
	u_strFromJavaModifiedUTF8WithSub
	u_strFromPunycode
	u_strFromUTF32
	u_strFromUTF32WithSub
	u_strFromUTF8
	u_strFromUTF8Lenient
	u_strFromUTF8WithSub
	u_strFromWCS
	u_strHasMoreChar32Than
	u_strToJavaModifiedUTF8
	u_strToLower
	u_strToPunycode
	u_strToTitle
	u_strToUTF32
	u_strToUTF32WithSub
	u_strToUTF8
	u_strToUTF8WithSub
	u_strToUpper
	u_strToWCS
	u_strcasecmp
	u_strcat
	u_strchr
	u_strchr32
	u_strcmp
	u_strcmpCodePointOrder
	u_strcpy
	u_strcspn
	u_stringHasBinaryProperty
	u_strlen
	u_strncasecmp
	u_strncat
	u_strncmp
	u_strncmpCodePointOrder
	u_strncpy
	u_strpbrk
	u_strrchr
	u_strrchr32
	u_strrstr
	u_strspn
	u_strstr
	u_strtok_r
	u_terminateChars
	u_terminateUChar32s
	u_terminateUChars
	u_terminateWChars
	u_tolower
	u_totitle
	u_toupper
	u_uastrcpy
	u_uastrncpy
	u_unescape
	u_unescapeAt
	u_versionFromString
	u_versionFromUString
	u_versionToString
	EOF
