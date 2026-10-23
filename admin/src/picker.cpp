#include "picker.h"

#include <ole2.h>      // IDataObject and the COM interface macros objsel.h relies on
#include <objsel.h>
#include <sddl.h>

#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")

namespace pvnc {

namespace {

// Resolves a name ("DOMAIN\\user", "user", UPN) to an SDDL SID via the SAM/AD.
std::wstring sidFromName(const std::wstring& name) {
  if (name.empty()) return {};
  DWORD cbSid = 0, cchDom = 0;
  SID_NAME_USE use{};
  LookupAccountNameW(nullptr, name.c_str(), nullptr, &cbSid, nullptr, &cchDom, &use);
  if (cbSid == 0) return {};
  std::vector<uint8_t> sid(cbSid);
  std::wstring dom(cchDom, L'\0');
  if (!LookupAccountNameW(nullptr, name.c_str(), sid.data(), &cbSid, dom.data(), &cchDom, &use))
    return {};
  LPWSTR str = nullptr;
  std::wstring out;
  if (ConvertSidToStringSidW(sid.data(), &str) && str) {
    out = str;
    LocalFree(str);
  }
  return out;
}

// Pulls an SDDL SID out of a fetched objectSid attribute (SAFEARRAY of bytes).
std::wstring sidFromAttr(const VARIANT& v) {
  if ((v.vt & VT_ARRAY) == 0 || (v.vt & VT_TYPEMASK) != VT_UI1 || !v.parray) return {};
  SAFEARRAY* sa = v.parray;
  void* data = nullptr;
  if (FAILED(SafeArrayAccessData(sa, &data))) return {};
  std::wstring out;
  if (IsValidSid(data)) {
    LPWSTR str = nullptr;
    if (ConvertSidToStringSidW(data, &str) && str) {
      out = str;
      LocalFree(str);
    }
  }
  SafeArrayUnaccessData(sa);
  return out;
}

bool isGroupClass(const wchar_t* cls) {
  return cls && wcsstr(cls, L"roup") != nullptr;  // "group", "Group"
}

}  // namespace

bool pickPrincipals(HWND owner, std::vector<Principal>& out) {
  HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool weInit = (coInit == S_OK || coInit == S_FALSE);

  IDsObjectPicker* picker = nullptr;
  bool picked = false;
  HRESULT hr = CoCreateInstance(CLSID_DsObjectPicker, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IDsObjectPicker, (void**)&picker);
  if (SUCCEEDED(hr) && picker) {
    DSOP_SCOPE_INIT_INFO scope{};
    scope.cbSize = sizeof(scope);
    scope.flType = DSOP_SCOPE_TYPE_TARGET_COMPUTER |
                   DSOP_SCOPE_TYPE_UPLEVEL_JOINED_DOMAIN |
                   DSOP_SCOPE_TYPE_DOWNLEVEL_JOINED_DOMAIN |
                   DSOP_SCOPE_TYPE_ENTERPRISE_DOMAIN | DSOP_SCOPE_TYPE_GLOBAL_CATALOG |
                   DSOP_SCOPE_TYPE_WORKGROUP;
    scope.flScope = DSOP_SCOPE_FLAG_STARTING_SCOPE | DSOP_SCOPE_FLAG_WANT_SID_PATH;
    const ULONG groupFilter = DSOP_FILTER_UNIVERSAL_GROUPS_SE | DSOP_FILTER_GLOBAL_GROUPS_SE |
                              DSOP_FILTER_DOMAIN_LOCAL_GROUPS_SE | DSOP_FILTER_BUILTIN_GROUPS;
    scope.FilterFlags.Uplevel.flBothModes = DSOP_FILTER_USERS | groupFilter;
    scope.FilterFlags.flDownlevel = DSOP_DOWNLEVEL_FILTER_USERS |
                                    DSOP_DOWNLEVEL_FILTER_LOCAL_GROUPS |
                                    DSOP_DOWNLEVEL_FILTER_GLOBAL_GROUPS |
                                    DSOP_DOWNLEVEL_FILTER_ALL_WELLKNOWN_SIDS;

    const wchar_t* attrSid = L"objectSid";
    DSOP_INIT_INFO init{};
    init.cbSize = sizeof(init);
    init.pwzTargetComputer = nullptr;  // local machine
    init.cDsScopeInfos = 1;
    init.aDsScopeInfos = &scope;
    init.flOptions = DSOP_FLAG_MULTISELECT;
    init.cAttributesToFetch = 1;
    init.apwzAttributeNames = const_cast<PCWSTR*>(&attrSid);

    if (SUCCEEDED(picker->Initialize(&init))) {
      IDataObject* dobj = nullptr;
      HRESULT dlg = picker->InvokeDialog(owner, &dobj);
      if (dlg == S_OK && dobj) {
        UINT cf = RegisterClipboardFormatW(CFSTR_DSOP_DS_SELECTION_LIST);
        FORMATETC fmt{(CLIPFORMAT)cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM med{};
        if (SUCCEEDED(dobj->GetData(&fmt, &med))) {
          auto* list = (PDS_SELECTION_LIST)GlobalLock(med.hGlobal);
          if (list) {
            for (ULONG i = 0; i < list->cItems; ++i) {
              const DS_SELECTION& sel = list->aDsSelection[i];
              Principal p;
              p.name = sel.pwzName ? sel.pwzName : L"";
              p.isGroup = isGroupClass(sel.pwzClass);
              if (sel.pvarFetchedAttributes && init.cAttributesToFetch >= 1)
                p.sid = sidFromAttr(sel.pvarFetchedAttributes[0]);
              if (p.sid.empty()) p.sid = sidFromName(p.name);
              if (!p.sid.empty()) {
                out.push_back(std::move(p));
                picked = true;
              }
            }
            GlobalUnlock(med.hGlobal);
          }
          ReleaseStgMedium(&med);
        }
        dobj->Release();
      }
    }
    picker->Release();
  }

  if (weInit) CoUninitialize();
  return picked;
}

}  // namespace pvnc
