#define NOMINMAX
#include <windows.h>
#include <windowsx.h>

#include "PianoRollLabAdapter.h"

#include <algorithm>
#include <string>

using pianoroll::InputModifiers;
using pianoroll::Point;
using pianoroll::Rect;
using pianoroll::Viewport;
using pianoroll_lab::Adapter;

namespace {
Adapter g_lab;
HWND g_window = nullptr;
bool g_left = false;
bool g_middle = false;

InputModifiers modifiers() {
    return {
        (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0,
    };
}

Point pointFromLParam(LPARAM value) {
    return {static_cast<double>(GET_X_LPARAM(value)),
            static_cast<double>(GET_Y_LPARAM(value))};
}

void fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void text(HDC dc, int x, int y, const std::wstring& value, COLORREF color) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, value.c_str(), static_cast<int>(value.size()));
}

void drawCanvas(HDC dc, RECT client) {
    fill(dc, client, RGB(22, 24, 28));
    const Viewport& view = g_lab.viewport();
    const Rect area = view.area;
    const int left = static_cast<int>(area.x);
    const int top = static_cast<int>(area.y);
    const int right = static_cast<int>(area.right());
    const int bottom = static_cast<int>(area.bottom());

    RECT canvas{left, top, right, bottom};
    fill(dc, canvas, RGB(30, 34, 40));

    const int firstPitch = std::max(0, view.yToPitch(area.bottom(), Viewport::PitchHit::floorFromUpperEdge) - 1);
    const int lastPitch = std::min(127, view.yToPitch(area.y, Viewport::PitchHit::ceilFromTop) + 1);
    for (int pitch = firstPitch; pitch <= lastPitch; ++pitch) {
        const int y = static_cast<int>(view.pitchToY(pitch));
        const bool black = pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 ||
                           pitch % 12 == 8 || pitch % 12 == 10;
        fill(dc, {0, y, left, y + static_cast<int>(view.rowHeight)},
             black ? RGB(28, 29, 33) : RGB(53, 55, 60));
        line(dc, left, y, right, y, pitch % 12 == 0 ? RGB(115, 120, 132) : RGB(67, 70, 78));
    }

    for (int beat = static_cast<int>(view.timeStart) - 1;
         beat <= static_cast<int>(view.timeStart + view.timeSpan) + 1; ++beat) {
        const int x = static_cast<int>(view.timeToX(static_cast<double>(beat)));
        if (x < left || x > right) continue;
        const bool bar = beat % 4 == 0;
        line(dc, x, top, x, bottom, bar ? RGB(105, 112, 128) : RGB(57, 62, 72));
    }

    for (const auto& note : g_lab.notes()) {
        const Rect r = view.noteRect(note.start, note.end, note.pitch, 10, 2,
                                     .5 - 10.0 / (2 * view.rowHeight));
        RECT noteRect{static_cast<int>(r.x), static_cast<int>(r.y),
                      static_cast<int>(r.right()), static_cast<int>(r.bottom())};
        COLORREF color = note.keySwitch ? RGB(188, 122, 224) :
                         note.generated ? RGB(232, 156, 76) :
                         note.selected ? RGB(76, 157, 245) : RGB(83, 111, 145);
        fill(dc, noteRect, color);
        FrameRect(dc, &noteRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    }

    if (g_lab.gesture().phase() == pianoroll::NoteGesturePhase::lassoing) {
        const Rect lasso = g_lab.gesture().lasso();
        HPEN pen = CreatePen(PS_DOT, 1, RGB(235, 235, 235));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
        Rectangle(dc, static_cast<int>(lasso.x), static_cast<int>(lasso.y),
                  static_cast<int>(lasso.right()), static_cast<int>(lasso.bottom()));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    fill(dc, {0, 0, client.right, 38}, RGB(18, 19, 22));
    text(dc, 12, 10, L"PianoRollLab | left-drag move | Shift-drag copy | edge resize | middle pan | wheel pitch | Ctrl+Z undo | Esc cancel",
         RGB(230, 232, 238));
    const std::string status8 = g_lab.status();
    std::wstring status(status8.begin(), status8.end());
    text(dc, 12, client.bottom - 28, status, RGB(190, 195, 205));
    text(dc, 12, client.bottom - 48, L"blue=selected | orange=copy | purple=Key Switch | real NoteGesture adapter",
         RGB(145, 151, 164));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            g_lab.resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            g_left = true;
            g_lab.mouseDown(pointFromLParam(lParam), modifiers());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (g_left) g_lab.mouseMove(pointFromLParam(lParam), modifiers());
            if (g_middle) g_lab.middleMove(pointFromLParam(lParam));
            if (g_left || g_middle) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (g_left) {
                g_left = false;
                g_lab.mouseUp(pointFromLParam(lParam), modifiers());
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_RBUTTONDOWN:
            g_lab.cancel();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MBUTTONDOWN:
            SetCapture(hwnd);
            g_middle = true;
            g_lab.middleDown(pointFromLParam(lParam));
            return 0;
        case WM_MBUTTONUP:
            g_middle = false;
            g_lab.middleUp();
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &screen);
            const double delta = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            g_lab.wheel({static_cast<double>(screen.x), static_cast<double>(screen.y)}, 0, delta, modifiers());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) g_lab.cancel();
            else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) g_lab.undo();
            else if (wParam == VK_HOME) g_lab.resetDemo();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            drawCanvas(dc, client);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    const wchar_t className[] = L"PianoRollLabWindow";
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&windowClass);

    g_window = CreateWindowExW(0, className, L"PianoRollLab - PianoRollCore",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
                               nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
