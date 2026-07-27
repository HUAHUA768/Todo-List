#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace
{

    constexpr int kWindowWidth = 450;
    constexpr int kWindowHeight = 720;
    constexpr int kMargin = 16;
    constexpr int kGap = 10;
    constexpr int kTitleBarHeight = 34;
    constexpr int kControlHeight = 24;
    constexpr int kButtonWidth = 96;
    constexpr int kIconButtonSize = 28;
    constexpr int kCaptionButtonWidth = 42;
    constexpr int kLabelWidth = 72;
    constexpr int kTimeFieldWidth = 110;
    constexpr int kListHeaderHeight = 24;
    constexpr int kListRowHeight = 24;
    constexpr int kDateFieldWidth = 150;

    constexpr wchar_t kMainClassName[] = L"TodoListWin32MainWindow";
    constexpr wchar_t kWindowTitle[] = L"待办表";

    constexpr int kIdTitleEdit = 1001;
    constexpr int kIdDatePicker = 1002;
    constexpr int kIdTimeCheck = 1003;
    constexpr int kIdTimePicker = 1004;
    constexpr int kIdAddButton = 1005;
    constexpr int kIdTopMostCheck = 1006;
    constexpr int kIdPendingList = 2001;
    constexpr int kIdCompletedList = 2002;

    struct TodoItem
    {
        std::wstring title;
        std::wstring dateText;
        std::wstring timeText;
        bool hasTime = false;
        bool completed = false;
    };

    HWND g_titleEdit = nullptr;
    HWND g_titleLabel = nullptr;
    HWND g_datePicker = nullptr;
    HWND g_dateLabel = nullptr;
    HWND g_timeCheck = nullptr;
    HWND g_timePicker = nullptr;
    HWND g_topMostCheck = nullptr;
    HWND g_minimizeButton = nullptr;
    HWND g_maximizeButton = nullptr;
    HWND g_closeButton = nullptr;
    HWND g_addButton = nullptr;
    HWND g_addConfirmButton = nullptr;
    HWND g_addCancelButton = nullptr;
    HWND g_pendingList = nullptr;
    HWND g_completedList = nullptr;
    HFONT g_iconFont = nullptr;
    bool g_addFormVisible = false;

    std::vector<std::unique_ptr<TodoItem>> g_items;
    bool g_internalStateChange = false;

    void ApplyTopMost(HWND hwnd)
    {
        const bool topMost = Button_GetCheck(g_topMostCheck) == BST_CHECKED;
        SetWindowPos(hwnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void ToggleMaximize(HWND hwnd)
    {
        if (IsZoomed(hwnd))
        {
            ShowWindow(hwnd, SW_RESTORE);
        }
        else
        {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
    }

    void ApplyIconFont(HWND control)
    {
        if (!g_iconFont)
        {
            g_iconFont = CreateFontW(
                20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_iconFont), TRUE);
    }

    void Relayout(HWND hwnd);

    void UpdateAddButtonLabel()
    {
        SetWindowTextW(g_addButton, g_addFormVisible ? L"收起" : L"添加待办");
    }

    void SetAddFormVisible(HWND hwnd, bool visible)
    {
        g_addFormVisible = visible;

        const int show = visible ? SW_SHOW : SW_HIDE;
        ShowWindow(g_titleLabel, show);
        ShowWindow(g_titleEdit, show);
        ShowWindow(g_dateLabel, show);
        ShowWindow(g_datePicker, show);
        ShowWindow(g_timeCheck, show);
        ShowWindow(g_timePicker, show);
        ShowWindow(g_addConfirmButton, show);
        ShowWindow(g_addCancelButton, show);

        UpdateAddButtonLabel();
        Relayout(hwnd);

        if (visible)
        {
            SetFocus(g_titleEdit);
        }
    }

    int ListViewGetItemCount(HWND listView);

    int MeasureListHeight(HWND listView)
    {
        const int itemCount = ListViewGetItemCount(listView);
        if (itemCount <= 0)
        {
            return 0;
        }
        return itemCount * kListRowHeight + 2;
    }

    std::wstring TrimmedText(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring text(static_cast<size_t>(length), L'\0');
        if (length > 0)
        {
            GetWindowTextW(control, text.data(), length + 1);
        }
        text.resize(static_cast<size_t>(length));

        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
        {
            return L"";
        }
        const auto last = text.find_last_not_of(L" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    std::wstring FormatDate(const SYSTEMTIME &time)
    {
        wchar_t buffer[32];
        wsprintfW(buffer, L"%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
        return buffer;
    }

    std::wstring FormatTime(const SYSTEMTIME &time)
    {
        wchar_t buffer[32];
        wsprintfW(buffer, L"%02u:%02u", time.wHour, time.wMinute);
        return buffer;
    }

    int ListViewGetItemCount(HWND listView)
    {
        return static_cast<int>(SendMessageW(listView, LVM_GETITEMCOUNT, 0, 0));
    }

    int ListViewInsertColumn(HWND listView, int index, const LVCOLUMNW &column)
    {
        return static_cast<int>(SendMessageW(listView, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&column)));
    }

    int ListViewInsertItem(HWND listView, const LVITEMW &item)
    {
        return static_cast<int>(SendMessageW(listView, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item)));
    }

    BOOL ListViewSetItemText(HWND listView, int itemIndex, int subItemIndex, LPWSTR text)
    {
        LVITEMW item{};
        item.iSubItem = subItemIndex;
        item.pszText = text;
        return static_cast<BOOL>(SendMessageW(listView, LVM_SETITEMTEXTW, static_cast<WPARAM>(itemIndex), reinterpret_cast<LPARAM>(&item)));
    }

    BOOL ListViewGetItem(HWND listView, LVITEMW &item)
    {
        return static_cast<BOOL>(SendMessageW(listView, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item)));
    }

    void ListViewDeleteAllItems(HWND listView)
    {
        SendMessageW(listView, LVM_DELETEALLITEMS, 0, 0);
    }

    void ListViewSetExtendedStyle(HWND listView, DWORD style)
    {
        SendMessageW(listView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, static_cast<LPARAM>(style));
    }

    void ListViewSetCheckState(HWND listView, int itemIndex, BOOL checked)
    {
        LVITEMW item{};
        item.stateMask = LVIS_STATEIMAGEMASK;
        item.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
        SendMessageW(listView, LVM_SETITEMSTATE, static_cast<WPARAM>(itemIndex), reinterpret_cast<LPARAM>(&item));
    }

    bool ListViewGetCheckState(HWND listView, int itemIndex)
    {
        return (static_cast<int>(SendMessageW(listView, LVM_GETITEMSTATE, static_cast<WPARAM>(itemIndex), LVIS_STATEIMAGEMASK)) >> 12) == 2;
    }

    void SetListColumns(HWND listView)
    {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        column.pszText = const_cast<LPWSTR>(L"事项");
        column.cx = 360;
        ListViewInsertColumn(listView, 0, column);

        column.pszText = const_cast<LPWSTR>(L"日期");
        column.cx = 150;
        ListViewInsertColumn(listView, 1, column);

        column.pszText = const_cast<LPWSTR>(L"时间");
        column.cx = 120;
        ListViewInsertColumn(listView, 2, column);
    }

    void AddTodoToList(HWND listView, TodoItem *item, bool withCheckbox)
    {
        LVITEMW listItem{};
        listItem.mask = LVIF_TEXT | LVIF_PARAM;
        listItem.iItem = ListViewGetItemCount(listView);
        listItem.iSubItem = 0;
        listItem.pszText = const_cast<LPWSTR>(item->title.c_str());
        listItem.lParam = reinterpret_cast<LPARAM>(item);
        const int row = ListViewInsertItem(listView, listItem);
        ListViewSetItemText(listView, row, 1, const_cast<LPWSTR>(item->dateText.c_str()));
        ListViewSetItemText(listView, row, 2, const_cast<LPWSTR>(item->hasTime ? item->timeText.c_str() : L""));

        if (withCheckbox)
        {
            ListViewSetCheckState(listView, row, FALSE);
        }
    }

    void RebuildLists()
    {
        if (!g_pendingList || !g_completedList)
        {
            return;
        }

        ListViewDeleteAllItems(g_pendingList);
        ListViewDeleteAllItems(g_completedList);

        for (const auto &item : g_items)
        {
            AddTodoToList(item->completed ? g_completedList : g_pendingList, item.get(), !item->completed);
        }
    }

    void MoveItemToCompleted(int index)
    {
        LVITEMW query{};
        query.mask = LVIF_PARAM;
        query.iItem = index;
        query.iSubItem = 0;
        if (!ListViewGetItem(g_pendingList, query))
        {
            return;
        }

        auto *item = reinterpret_cast<TodoItem *>(query.lParam);
        if (!item || item->completed)
        {
            return;
        }

        item->completed = true;
        RebuildLists();
    }

    void UpdateTimePickerEnabled()
    {
        const BOOL checked = Button_GetCheck(g_timeCheck) == BST_CHECKED;
        EnableWindow(g_timePicker, checked);
    }

    void AddTodoFromInputs(HWND owner)
    {
        const std::wstring title = TrimmedText(g_titleEdit);
        if (title.empty())
        {
            MessageBoxW(owner, L"请输入待办事项。", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }

        SYSTEMTIME dateTime{};
        if (DateTime_GetSystemtime(g_datePicker, &dateTime) != GDT_VALID)
        {
            MessageBoxW(owner, L"请选择有效日期。", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const bool hasTime = Button_GetCheck(g_timeCheck) == BST_CHECKED;
        SYSTEMTIME timeTime{};
        std::wstring timeText;
        if (hasTime)
        {
            if (DateTime_GetSystemtime(g_timePicker, &timeTime) != GDT_VALID)
            {
                MessageBoxW(owner, L"请选择有效时间。", L"提示", MB_OK | MB_ICONINFORMATION);
                return;
            }
            timeText = FormatTime(timeTime);
        }

        auto item = std::make_unique<TodoItem>();
        item->title = title;
        item->dateText = FormatDate(dateTime);
        item->hasTime = hasTime;
        item->timeText = timeText;

        g_items.push_back(std::move(item));
        AddTodoToList(g_pendingList, g_items.back().get(), true);

        SetWindowTextW(g_titleEdit, L"");
        SetAddFormVisible(owner, false);
        SendMessageW(g_titleEdit, EM_SETSEL, 0, -1);
    }

    void CreateInputControls(HWND hwnd)
    {
        const int top = kTitleBarHeight + kMargin;
        g_titleLabel = CreateWindowExW(0, L"STATIC", L"事项", WS_CHILD,
                                       kMargin, top + 4, kLabelWidth, kControlHeight, hwnd, nullptr, nullptr, nullptr);

        g_titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                      kMargin + kLabelWidth + kGap, top,
                                      270, kControlHeight, hwnd, reinterpret_cast<HMENU>(kIdTitleEdit), nullptr, nullptr);

        g_dateLabel = CreateWindowExW(0, L"STATIC", L"日期", WS_CHILD,
                                      kMargin, top + 34 + 4, kLabelWidth, kControlHeight, hwnd, nullptr, nullptr, nullptr);

        g_datePicker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_TABSTOP | DTS_SHORTDATEFORMAT,
                                       kMargin + kLabelWidth + kGap, top + 34,
                                       150, 200, hwnd, reinterpret_cast<HMENU>(kIdDatePicker), nullptr, nullptr);

        g_timeCheck = CreateWindowExW(0, L"BUTTON", L"包含时间", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                      kMargin, top + 68, 96, kControlHeight, hwnd, reinterpret_cast<HMENU>(kIdTimeCheck), nullptr, nullptr);

        g_timePicker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_TABSTOP | DTS_TIMEFORMAT,
                                       kMargin + 96 + kGap, top + 66,
                                       kTimeFieldWidth, 200, hwnd, reinterpret_cast<HMENU>(kIdTimePicker), nullptr, nullptr);

        g_topMostCheck = CreateWindowExW(0, L"BUTTON", L"\uE718",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE,
                                         0, 0, kCaptionButtonWidth, kTitleBarHeight - 4,
                                         hwnd, reinterpret_cast<HMENU>(kIdTopMostCheck), nullptr, nullptr);
        ApplyIconFont(g_topMostCheck);
        SetWindowTextW(g_topMostCheck, L"\uE718");

        g_minimizeButton = CreateWindowExW(0, L"BUTTON", L"\uE921",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                           0, 0, kCaptionButtonWidth, kTitleBarHeight - 4,
                                           hwnd, reinterpret_cast<HMENU>(1010), nullptr, nullptr);
        ApplyIconFont(g_minimizeButton);

        g_maximizeButton = CreateWindowExW(0, L"BUTTON", L"\uE922",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                           0, 0, kCaptionButtonWidth, kTitleBarHeight - 4,
                                           hwnd, reinterpret_cast<HMENU>(1011), nullptr, nullptr);
        ApplyIconFont(g_maximizeButton);

        g_closeButton = CreateWindowExW(0, L"BUTTON", L"\uE8BB",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, kCaptionButtonWidth, kTitleBarHeight - 4,
                                        hwnd, reinterpret_cast<HMENU>(1012), nullptr, nullptr);
        ApplyIconFont(g_closeButton);

        g_addButton = CreateWindowExW(0, L"BUTTON", L"添加待办", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      kMargin, top,
                                      kButtonWidth, kControlHeight + 2, hwnd, reinterpret_cast<HMENU>(kIdAddButton), nullptr, nullptr);

        g_addConfirmButton = CreateWindowExW(0, L"BUTTON", L"添加", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,
                             kMargin, top + 104,
                             96, kControlHeight + 2, hwnd, reinterpret_cast<HMENU>(1008), nullptr, nullptr);

        g_addCancelButton = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_TABSTOP,
                                            kMargin + 96 + kGap, top + 104,
                                            96, kControlHeight + 2, hwnd, reinterpret_cast<HMENU>(1007), nullptr, nullptr);

        UpdateTimePickerEnabled();
        SetAddFormVisible(hwnd, false);
    }

    void CreateListSection(HWND hwnd)
    {
        const int sectionTop = kTitleBarHeight + kMargin + (g_addFormVisible ? 180 : 40);
        RECT client{};
        GetClientRect(hwnd, &client);
        const int sectionWidth = client.right - client.left - kMargin * 2;

        CreateWindowExW(0, L"STATIC", L"待办", WS_CHILD | WS_VISIBLE, kMargin, sectionTop, 80, kListHeaderHeight, hwnd, nullptr, nullptr, nullptr);
        g_pendingList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                        kMargin, sectionTop + kListHeaderHeight, sectionWidth, 0, hwnd,
                                        reinterpret_cast<HMENU>(kIdPendingList), nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"已完成", WS_CHILD | WS_VISIBLE, kMargin, sectionTop + kListHeaderHeight, 80, kListHeaderHeight, hwnd, nullptr, nullptr, nullptr);
        g_completedList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                          kMargin, sectionTop + kListHeaderHeight, sectionWidth, 0, hwnd,
                                          reinterpret_cast<HMENU>(kIdCompletedList), nullptr, nullptr);

        ListViewSetExtendedStyle(g_pendingList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES);
        ListViewSetExtendedStyle(g_completedList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        SetListColumns(g_pendingList);
        SetListColumns(g_completedList);
    }

    void InitializeControls(HWND hwnd)
    {
        CreateInputControls(hwnd);
        CreateListSection(hwnd);
    }

    void Relayout(HWND hwnd)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        const int top = kTitleBarHeight + kMargin;
        const int contentWidth = width - kMargin * 2;
        const int titleWidth = std::max(180, contentWidth - (kButtonWidth + kGap));

        MoveWindow(g_addButton, kMargin, top, kButtonWidth, kControlHeight + 2, TRUE);
        MoveWindow(g_topMostCheck, width - kMargin - kCaptionButtonWidth * 4, 2, kCaptionButtonWidth, kTitleBarHeight - 4, TRUE);
        MoveWindow(g_minimizeButton, width - kMargin - kCaptionButtonWidth * 3, 2, kCaptionButtonWidth, kTitleBarHeight - 4, TRUE);
        MoveWindow(g_maximizeButton, width - kMargin - kCaptionButtonWidth * 2, 2, kCaptionButtonWidth, kTitleBarHeight - 4, TRUE);
        MoveWindow(g_closeButton, width - kMargin - kCaptionButtonWidth, 2, kCaptionButtonWidth, kTitleBarHeight - 4, TRUE);
        if (g_addFormVisible)
        {
            MoveWindow(g_titleLabel, kMargin, top + 40 + 4, kLabelWidth, kControlHeight, TRUE);
            MoveWindow(g_titleEdit, kMargin + kLabelWidth + kGap, top + 40, titleWidth, kControlHeight, TRUE);
            MoveWindow(g_dateLabel, kMargin, top + 76 + 4, kLabelWidth, kControlHeight, TRUE);
            MoveWindow(g_datePicker, kMargin + kLabelWidth + kGap, top + 76, kDateFieldWidth, 200, TRUE);
            MoveWindow(g_timeCheck, kMargin, top + 110, 96, kControlHeight, TRUE);
            MoveWindow(g_timePicker, kMargin + 96 + kGap, top + 108, kTimeFieldWidth, 200, TRUE);
            MoveWindow(g_addCancelButton, kMargin + 96 + kGap, top + 144, 96, kControlHeight + 2, TRUE);
        }
        else
        {
            MoveWindow(g_titleLabel, 0, 0, 0, 0, TRUE);
            MoveWindow(g_titleEdit, 0, 0, 0, 0, TRUE);
            MoveWindow(g_dateLabel, 0, 0, 0, 0, TRUE);
            MoveWindow(g_datePicker, 0, 0, 0, 0, TRUE);
            MoveWindow(g_timeCheck, 0, 0, 0, 0, TRUE);
            MoveWindow(g_timePicker, 0, 0, 0, 0, TRUE);
            MoveWindow(g_addCancelButton, 0, 0, 0, 0, TRUE);
        }

        const int sectionTop = kTitleBarHeight + kMargin + (g_addFormVisible ? 180 : 40);
        const int sectionWidth = width - kMargin * 2;
        const int pendingListHeight = MeasureListHeight(g_pendingList);
        const int completedListHeight = MeasureListHeight(g_completedList);

        MoveWindow(g_pendingList, kMargin, sectionTop + kListHeaderHeight, sectionWidth, pendingListHeight, TRUE);
        MoveWindow(g_completedList, kMargin, sectionTop + kListHeaderHeight + pendingListHeight + 20 + kListHeaderHeight, sectionWidth, completedListHeight, TRUE);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            InitializeControls(hwnd);
            return 0;
        }
        case WM_SIZE:
        {
            Relayout(hwnd);
            return 0;
        }
        case WM_COMMAND:
        {
            const int controlId = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (controlId == kIdAddButton && notification == BN_CLICKED)
            {
                SetAddFormVisible(hwnd, !g_addFormVisible);
                return 0;
            }

            if (controlId == 1008 && notification == BN_CLICKED)
            {
                AddTodoFromInputs(hwnd);
                return 0;
            }

            if (controlId == 1007 && notification == BN_CLICKED)
            {
                SetAddFormVisible(hwnd, false);
                return 0;
            }

            if (controlId == kIdTimeCheck && notification == BN_CLICKED)
            {
                UpdateTimePickerEnabled();
                return 0;
            }

            if (controlId == kIdTopMostCheck && notification == BN_CLICKED)
            {
                SetWindowTextW(g_topMostCheck, Button_GetCheck(g_topMostCheck) == BST_CHECKED ? L"\uE718" : L"\uE7B8");
                ApplyTopMost(hwnd);
                return 0;
            }

            if (controlId == 1010 && notification == BN_CLICKED)
            {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }

            if (controlId == 1011 && notification == BN_CLICKED)
            {
                ToggleMaximize(hwnd);
                return 0;
            }

            if (controlId == 1012 && notification == BN_CLICKED)
            {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        }
        case WM_NCHITTEST:
        {
            const LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit == HTCLIENT)
            {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                if (pt.y >= 0 && pt.y < kTitleBarHeight)
                {
                    return HTCAPTION;
                }
            }
            return hit;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int clientWidth = client.right - client.left;
            RECT titleBar{0, 0, ps.rcPaint.right, kTitleBarHeight};
            HBRUSH brush = CreateSolidBrush(RGB(245, 245, 245));
            FillRect(hdc, &titleBar, brush);
            DeleteObject(brush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(30, 30, 30));
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));
            RECT textRect{kMargin, 0, clientWidth - kMargin * 4 - kCaptionButtonWidth * 4, kTitleBarHeight};
            DrawTextW(hdc, kWindowTitle, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NOTIFY:
        {
            auto *header = reinterpret_cast<NMHDR *>(lParam);
            if (header->idFrom == kIdPendingList && header->code == LVN_ITEMCHANGED && !g_internalStateChange)
            {
                auto *view = reinterpret_cast<NMLISTVIEW *>(lParam);
                if ((view->uChanged & LVIF_STATE) != 0 && ListViewGetCheckState(g_pendingList, view->iItem))
                {
                    g_internalStateChange = true;
                    MoveItemToCompleted(view->iItem);
                    g_internalStateChange = false;
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            if (g_iconFont)
            {
                DeleteObject(g_iconFont);
                g_iconFont = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kMainClassName;

    if (!RegisterClassW(&windowClass))
    {
        return 0;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kMainClassName, kWindowTitle, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        return 0;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
