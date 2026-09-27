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

COLORREF mixColor(COLORREF a, COLORREF b, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    auto mix = [&](int x, int y) { return static_cast<BYTE>(std::clamp(
        static_cast<int>(std::lround(x + (y - x) * amount)), 0, 255)); };
    return RGB(mix(GetRValue(a), GetRValue(b)),
               mix(GetGValue(a), GetGValue(b)),
               mix(GetBValue(a), GetBValue(b)));
}

COLORREF visualColor(pianoroll::VisualColor color) {
    auto channel = [](double value) -> BYTE {
        return static_cast<BYTE>(std::clamp(static_cast<int>(std::lround(
            std::clamp(value, 0.0, 1.0) * 255.0)), 0, 255));
    };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

void setCursorHint(const pianoroll::PointerFeedback& feedback) {
    LPCWSTR cursor = MAKEINTRESOURCEW(32512);
    switch (feedback.cursor) {
        case pianoroll::CursorHint::pencil: cursor = MAKEINTRESOURCEW(32515); break;
        case pianoroll::CursorHint::move: cursor = MAKEINTRESOURCEW(32646); break;
        case pianoroll::CursorHint::resizeHorizontal: cursor = MAKEINTRESOURCEW(32644); break;
        case pianoroll::CursorHint::lasso: cursor = MAKEINTRESOURCEW(32515); break;
        case pianoroll::CursorHint::select: cursor = MAKEINTRESOURCEW(32649); break;
        default: break;
    }
    SetCursor(LoadCursorW(nullptr, cursor));
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
    const auto& feedback = g_lab.feedback();
    int activePitch = -1;
    if (feedback.hit.valid() && feedback.hit.index < g_lab.notes().size())
        activePitch = g_lab.notes()[feedback.hit.index].pitch;
    for (int pitch = firstPitch; pitch <= lastPitch; ++pitch) {
        const int y = static_cast<int>(view.pitchToY(pitch));
        const bool black = pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 ||
                           pitch % 12 == 8 || pitch % 12 == 10;
        fill(dc, {0, y, left, y + static_cast<int>(view.rowHeight)},
             visualColor(pianoroll::keyboardRowColor(
                 pitch, black, pitch == activePitch,
                 pitch == activePitch && feedback.note == pianoroll::NoteFeedbackState::pressed)));
        line(dc, left, y, right, y, pitch % 12 == 0 ? RGB(115, 120, 132) : RGB(67, 70, 78));
    }

    for (int beat = static_cast<int>(view.timeStart) - 1;
         beat <= static_cast<int>(view.timeStart + view.timeSpan) + 1; ++beat) {
        const int x = static_cast<int>(view.timeToX(static_cast<double>(beat)));
        if (x < left || x > right) continue;
        const bool bar = beat % 4 == 0;
        line(dc, x, top, x, bottom, bar ? RGB(105, 112, 128) : RGB(57, 62, 72));
    }

    std::size_t noteIndex = 0;
    for (const auto& note : g_lab.notes()) {
        const Rect r = view.noteRect(note.start, note.end, note.pitch, 10, 2,
                                     .5 - 10.0 / (2 * view.rowHeight));
        RECT noteRect{static_cast<int>(r.x), static_cast<int>(r.y),
                      static_cast<int>(r.right()), static_cast<int>(r.bottom())};
        const auto state = feedback.hit.valid() && feedback.hit.index == noteIndex ?
                           feedback.note : (note.selected ? pianoroll::NoteFeedbackState::selected :
                                             pianoroll::NoteFeedbackState::normal);
        const auto visual = pianoroll::deriveNoteVisual(note, state,
                                                         note.pitch == activePitch, r.width);
        const COLORREF color = visualColor(visual.fill);
        fill(dc, noteRect, color);
        if (visual.showVelocityBar) {
            RECT bar{noteRect.left, noteRect.bottom - 3,
                     noteRect.left + static_cast<int>(r.width * visual.velocityBarFraction),
                     noteRect.bottom};
            fill(dc, bar, mixColor(color, visualColor(visual.velocityBar),
                                   visual.velocityBarAlpha));
        }
        const COLORREF border = visualColor(visual.border);
        HPEN borderPen = CreatePen(PS_SOLID, note.selected ? 2 : 1, border);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
        Rectangle(dc, noteRect.left, noteRect.top, noteRect.right, noteRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);
        ++noteIndex;
    }

    for (const auto& effect : g_lab.effects().fades()) {
        const double opacity = pianoroll::fadeOpacity(effect, g_lab.presentationNow());
        if (opacity <= 0) continue;
        const auto& note = effect.snapshot;
        const Rect r = view.noteRect(note.start, note.end, note.pitch, 10, 2,
                                     .5 - 10.0 / (2 * view.rowHeight));
        const auto visual = pianoroll::deriveNoteVisual(note,
            pianoroll::NoteFeedbackState::normal, false, r.width);
        RECT fadeRect{static_cast<int>(r.x), static_cast<int>(r.y),
                      static_cast<int>(r.right()), static_cast<int>(r.bottom())};
        fill(dc, fadeRect, mixColor(RGB(30, 34, 40), visualColor(visual.fill), opacity));
    }

    const Rect selection = g_lab.gesture().phase() == pianoroll::NoteGesturePhase::lassoing ||
                           g_lab.gesture().mode() == pianoroll::NoteGestureMode::scaleSelection ?
                           g_lab.gesture().lasso() : g_lab.selectionBounds();
    if (selection.width > 0 && selection.height > 0) {
        const Rect lasso = selection;
        HPEN pen = CreatePen(PS_DOT, 1, RGB(235, 235, 235));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
        Rectangle(dc, static_cast<int>(lasso.x), static_cast<int>(lasso.y),
                  static_cast<int>(lasso.right()), static_cast<int>(lasso.bottom()));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        if (feedback.showResizeHandle) {
            const Rect handle = feedback.resizeHandle;
            const int cx = static_cast<int>(handle.x + handle.width * .5);
            const int cy = static_cast<int>(handle.y + handle.height * .5);
            HPEN handlePen = CreatePen(PS_SOLID, 1, RGB(140, 180, 220));
            HPEN old = static_cast<HPEN>(SelectObject(dc, handlePen));
            MoveToEx(dc, cx - 7, cy, nullptr); LineTo(dc, cx + 7, cy);
            MoveToEx(dc, cx - 7, cy, nullptr); LineTo(dc, cx - 3, cy - 4);
            MoveToEx(dc, cx - 7, cy, nullptr); LineTo(dc, cx - 3, cy + 4);
            MoveToEx(dc, cx + 7, cy, nullptr); LineTo(dc, cx + 3, cy - 4);
            MoveToEx(dc, cx + 7, cy, nullptr); LineTo(dc, cx + 3, cy + 4);
            SelectObject(dc, old);
            DeleteObject(handlePen);
        }
    }

    fill(dc, {0, 0, client.right, 38}, RGB(18, 19, 22));
    text(dc, 12, 10, L"PianoRollLab | 1 pencil 2 pointer 3 eraser 4 slice 5 glue | left-drag move | Shift copy | Ctrl+Z undo",
         RGB(230, 232, 238));
    const std::string status8 = g_lab.status();
    std::wstring status(status8.begin(), status8.end());
    text(dc, 12, client.bottom - 28, status, RGB(190, 195, 205));
    text(dc, 12, client.bottom - 48, L"blue=selected | lighter=lower velocity | orange=copy | overlap click cycles | real NoteGesture adapter",
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
            setCursorHint(g_lab.feedback());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            g_lab.hover(pointFromLParam(lParam), modifiers());
            if (g_left) g_lab.mouseMove(pointFromLParam(lParam), modifiers());
            if (g_middle) g_lab.middleMove(pointFromLParam(lParam));
            setCursorHint(g_lab.feedback());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (g_left) {
                g_left = false;
                g_lab.mouseUp(pointFromLParam(lParam), modifiers());
                setCursorHint(g_lab.feedback());
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
        case WM_TIMER:
            if (wParam == 1) {
                g_lab.tick(static_cast<double>(GetTickCount64()) / 1000.0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) g_lab.cancel();
            else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) g_lab.undo();
            else if (wParam == VK_DELETE) g_lab.eraseSelected();
            else if (wParam == VK_HOME) g_lab.resetDemo();
            else if (wParam == '1') g_lab.setTool(pianoroll::ToolMode::pencil);
            else if (wParam == '2') g_lab.setTool(pianoroll::ToolMode::pointer);
            else if (wParam == '3') g_lab.setTool(pianoroll::ToolMode::eraser);
            else if (wParam == '4') g_lab.setTool(pianoroll::ToolMode::slice);
            else if (wParam == '5') g_lab.setTool(pianoroll::ToolMode::glue);
            else if (wParam == '6') g_lab.setTool(pianoroll::ToolMode::paint);
            else if (wParam == '7') g_lab.setTool(pianoroll::ToolMode::mute);
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
            KillTimer(hwnd, 1);
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
    SetTimer(g_window, 1, 16, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
