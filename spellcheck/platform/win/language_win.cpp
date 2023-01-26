// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/win/language_win.h"

#include "base/platform/win/base_windows_safe_library.h"

#include <Windows.h>
#include <ElsCore.h>
#include <ElsSrvc.h> // ELS_GUID_LANGUAGE_DETECTION.

namespace Platform::Language {
namespace {

HRESULT (__stdcall *MappingGetServices)(
	_In_opt_ PMAPPING_ENUM_OPTIONS pOptions,
	_Out_ PMAPPING_SERVICE_INFO *prgServices,
	_Out_ DWORD *pdwServicesCount
	);

HRESULT (__stdcall *MappingFreeServices)(
	_In_ PMAPPING_SERVICE_INFO pServiceInfo
	);

HRESULT (__stdcall *MappingRecognizeText)(
	_In_ PMAPPING_SERVICE_INFO pServiceInfo,
	_In_reads_(dwLength) LPCWSTR pszText,
	_In_ DWORD dwLength,
	_In_ DWORD dwIndex,
	_In_opt_ PMAPPING_OPTIONS pOptions,
	_Inout_ PMAPPING_PROPERTY_BAG pbag
	);

HRESULT (__stdcall *MappingFreePropertyBag)(
	_In_ PMAPPING_PROPERTY_BAG pBag
	);

[[nodiscard]] inline bool Supported() {
	static const auto Result = [] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto els = base::Platform::SafeLoadLibrary(L"elscore.dll");
		return LOAD_SYMBOL(els, MappingGetServices)
			&& LOAD_SYMBOL(els, MappingRecognizeText)
			&& LOAD_SYMBOL(els, MappingFreeServices)
			&& LOAD_SYMBOL(els, MappingFreePropertyBag);
#undef LOAD_SYMBOL
	}();
	return Result;
}

struct unique_services final {
	operator MAPPING_SERVICE_INFO *() {
		return services;
	}
	MAPPING_SERVICE_INFO **operator&() {
		return &services;
	}
	MAPPING_SERVICE_INFO *services = nullptr;
	unique_services() = default;
	unique_services(unique_services const &other) = delete;
	~unique_services() {
		if (services) {
			MappingFreeServices(services);
		}
	}
};

struct unique_bag final : public MAPPING_PROPERTY_BAG {
	unique_bag() : MAPPING_PROPERTY_BAG{} {
	}
	unique_bag(unique_bag const &other) = delete;
	~unique_bag() {
		MappingFreePropertyBag(this);
	}
};

inline void MappingRecognizeTextFromService(
		REFGUID service,
		LPCWSTR text,
		DWORD length,
		unique_bag &bag) {
	auto options = MAPPING_ENUM_OPTIONS{};
	options.Size = sizeof(options);
	options.pGuid = const_cast<GUID*>(&service);

	auto dwServicesCount = DWORD(0);
	auto services = unique_services();

	const auto hr = MappingGetServices(&options, &services, &dwServicesCount);
	if (FAILED(hr)) {
		return;
	}

	bag.Size = sizeof(bag);
	MappingRecognizeText(services, text, length, 0, nullptr, &bag);
}

} // namespace

void RecognizeTextLanguages(
		LPCWSTR text,
		DWORD length,
		Fn<void(LPCWSTR, int)> &&callback) {
	if (!length) {
		return;
	}

	auto bag = unique_bag();
	MappingRecognizeTextFromService(
		ELS_GUID_LANGUAGE_DETECTION,
		text,
		length,
		bag);

	auto pos = reinterpret_cast<LPCWSTR>(bag.prgResultRanges[0].pData);
	for (; *pos;) {
		const auto len = wcslen(pos);
		if (len >= 2) {
			callback(pos, len);
		}
		pos += (len + 1);
	}
}

Id Recognize(QStringView text) {
	if (Supported()) {
		auto locales = std::vector<QLocale>();
		RecognizeTextLanguages(
			(LPCWSTR)text.utf16(),
			DWORD(text.size()),
			[&](LPCWSTR r, int length) {
				// Cut complex result, e.g. "sr-Cyrl".
				locales.emplace_back(QString::fromWCharArray(r, 2));
			});
		return { locales[0].language() };
	}
	return {};
}

} // namespace Platform::Language
