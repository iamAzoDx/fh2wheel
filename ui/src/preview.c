#include <math.h>
#include <stdio.h>
#include <string.h>

#include "preview.h"
#include "preview_model.h"
#include "ui_theme.h"

#define UI_PREVIEW_CLASS L"fh2wheel-preview"
#define UI_TEXT_CAPACITY 64
#define UI_PADDING 18
#define UI_CORNER 14
#define UI_SUPERSAMPLE 3
#define UI_PI 3.14159265f
#define UI_DEGREES_TO_RADIANS (UI_PI / 180.0f)

#define UI_TITLE_TOP 16
#define UI_NAME_TOP 34
#define UI_WHEEL_TOP 70
#define UI_WHEEL_RADIUS 112
#define UI_WHEEL_MARGIN 8
#define UI_RIM_WIDTH 20
#define UI_HUB_RADIUS 38
#define UI_HUB_RING_RADIUS 24
#define UI_SPOKE_HUB_HALF 13
#define UI_SPOKE_RIM_HALF 20
#define UI_SPOKE_SIDE_ANGLE 80.0f
#define UI_MARKER_HALF 7
#define UI_EDGE_PEN 2
#define UI_HUB_RING_PEN 3

#define UI_PEDALS_TOP 392
#define UI_PEDAL_WIDTH 44
#define UI_PEDAL_HEIGHT 200
#define UI_PEDAL_RADIUS 10

#define UI_LOWER_TOP 700
#define UI_GEAR_SIZE 90
#define UI_CHIP_WIDTH 44
#define UI_CHIP_HEIGHT 26
#define UI_CHIP_GAP 6
#define UI_CHIP_COLUMNS 4
#define UI_CHIP_RADIUS 8
#define UI_SECTION_GAP 22

#define UI_FONT_TITLE 13
#define UI_FONT_NAME 18
#define UI_FONT_LABEL 13
#define UI_FONT_ANGLE 30
#define UI_FONT_GEAR 56
#define UI_FONT_CHIP 12

typedef struct {
    fhw_pedal pedal;
    const wchar_t *label;
    COLORREF color;
} ui_pedal_view;

typedef struct {
    fhw_button button;
    const wchar_t *label;
} ui_chip_view;

static const ui_pedal_view ui_pedal_views[] = {{FHW_PEDAL_CLUTCH, L"CLUTCH", UI_COLOR_CLUTCH},
                                               {FHW_PEDAL_BRAKE, L"BRAKE", UI_COLOR_BRAKE},
                                               {FHW_PEDAL_THROTTLE, L"THROTTLE", UI_COLOR_THROTTLE},
                                               {FHW_PEDAL_HANDBRAKE, L"HANDBRAKE", UI_COLOR_HANDBRAKE}};
static const ui_chip_view ui_chip_views[] = {
    {FHW_BUTTON_A, L"A"},         {FHW_BUTTON_B, L"B"},          {FHW_BUTTON_X, L"X"},
    {FHW_BUTTON_Y, L"Y"},         {FHW_BUTTON_UP, L"▲"},         {FHW_BUTTON_DOWN, L"▼"},
    {FHW_BUTTON_LEFT, L"◀"},      {FHW_BUTTON_RIGHT, L"▶"},      {FHW_BUTTON_VIEW, L"VIEW"},
    {FHW_BUTTON_MENU, L"MENU"},   {FHW_BUTTON_PADDLE_LEFT, L"LP"}, {FHW_BUTTON_PADDLE_RIGHT, L"RP"}};

static ui_preview_state ui_state;
static HFONT ui_font_title;
static HFONT ui_font_name;
static HFONT ui_font_label;
static HFONT ui_font_angle;
static HFONT ui_font_gear;
static HFONT ui_font_chip;

static HFONT ui_make_font(int height, int weight, const wchar_t *face)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void ui_fill_round(HDC dc, int left, int top, int right, int bottom, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);

    RoundRect(dc, left, top, right, bottom, radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void ui_text_at(HDC dc, HFONT font, COLORREF color, const wchar_t *text, int left, int top, int right,
                       int bottom, UINT format)
{
    RECT box = {left, top, right, bottom};
    HGDIOBJ old_font = SelectObject(dc, font);

    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &box, format | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static POINT ui_rotate(float x, float y, float degrees, int center)
{
    float radians = degrees * UI_DEGREES_TO_RADIANS;
    float cosine = cosf(radians);
    float sine = sinf(radians);
    POINT point;

    point.x = (LONG)((float)center + (x * cosine - y * sine) * UI_SUPERSAMPLE);
    point.y = (LONG)((float)center + (x * sine + y * cosine) * UI_SUPERSAMPLE);
    return point;
}

static void ui_polygon(HDC dc, const float (*corners)[2], int count, float degrees, int center, COLORREF color)
{
    POINT points[4];
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    int index;

    for (index = 0; index < count; index++)
        points[index] = ui_rotate(corners[index][0], corners[index][1], degrees, center);
    Polygon(dc, points, count);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void ui_circle(HDC dc, int center, int radius, COLORREF fill, COLORREF edge, int edge_width)
{
    HBRUSH brush = fill == CLR_INVALID ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, edge_width * UI_SUPERSAMPLE, edge);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    int scaled = radius * UI_SUPERSAMPLE;

    Ellipse(dc, center - scaled, center - scaled, center + scaled, center + scaled);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    if (fill != CLR_INVALID)
        DeleteObject(brush);
    DeleteObject(pen);
}

static void ui_draw_spokes(HDC dc, int center, float angle)
{
    static const float spoke_angles[] = {0.0f, UI_SPOKE_SIDE_ANGLE, -UI_SPOKE_SIDE_ANGLE};
    const float hub = (float)UI_HUB_RADIUS - 4.0f;
    const float rim = (float)(UI_WHEEL_RADIUS - UI_RIM_WIDTH) + 2.0f;
    const float spoke[4][2] = {{-UI_SPOKE_HUB_HALF, hub},
                               {UI_SPOKE_HUB_HALF, hub},
                               {UI_SPOKE_RIM_HALF, rim},
                               {-UI_SPOKE_RIM_HALF, rim}};
    unsigned int index;

    for (index = 0; index < sizeof(spoke_angles) / sizeof(spoke_angles[0]); index++)
        ui_polygon(dc, spoke, 4, spoke_angles[index] + angle, center, UI_COLOR_SPOKE);
}

static void ui_draw_wheel(HDC target, int center_x, int center_y, float angle)
{
    const int size = 2 * (UI_WHEEL_RADIUS + UI_WHEEL_MARGIN);
    const int scaled = size * UI_SUPERSAMPLE;
    const int center = scaled / 2;
    const float marker[4][2] = {{-UI_MARKER_HALF, -UI_WHEEL_RADIUS + 1},
                                {UI_MARKER_HALF, -UI_WHEEL_RADIUS + 1},
                                {UI_MARKER_HALF, -UI_WHEEL_RADIUS + UI_RIM_WIDTH - 1},
                                {-UI_MARKER_HALF, -UI_WHEEL_RADIUS + UI_RIM_WIDTH - 1}};
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, scaled, scaled);
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    RECT all = {0, 0, scaled, scaled};
    HBRUSH card = CreateSolidBrush(UI_COLOR_CARD);

    FillRect(dc, &all, card);
    DeleteObject(card);
    ui_circle(dc, center, UI_WHEEL_RADIUS, UI_COLOR_RIM, UI_COLOR_RIM_EDGE, UI_EDGE_PEN);
    ui_circle(dc, center, UI_WHEEL_RADIUS - UI_RIM_WIDTH, UI_COLOR_CARD, UI_COLOR_RIM_EDGE, UI_EDGE_PEN);
    ui_draw_spokes(dc, center, angle);
    ui_polygon(dc, marker, 4, angle, center, UI_COLOR_ACCENT);
    ui_circle(dc, center, UI_HUB_RADIUS, UI_COLOR_HUB, UI_COLOR_RIM_EDGE, UI_EDGE_PEN);
    ui_circle(dc, center, UI_HUB_RING_RADIUS, CLR_INVALID, UI_COLOR_ACCENT, UI_HUB_RING_PEN);

    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, NULL);
    StretchBlt(target, center_x - size / 2, center_y - size / 2, size, size, dc, 0, 0, scaled, scaled, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void ui_draw_header(HDC dc, int width)
{
    ui_text_at(dc, ui_font_title, UI_COLOR_MUTED, L"LIVE PREVIEW", UI_PADDING, UI_TITLE_TOP, width - UI_PADDING,
               UI_NAME_TOP, DT_LEFT);
    ui_text_at(dc, ui_font_name, ui_state.connected ? UI_COLOR_TEXT : UI_COLOR_MUTED,
               ui_state.connected ? ui_state.device_name : L"No wheel selected", UI_PADDING, UI_NAME_TOP,
               width - UI_PADDING, UI_NAME_TOP + 24, DT_LEFT | DT_END_ELLIPSIS);
}

static void ui_draw_steering(HDC dc, int width)
{
    const int center_y = UI_WHEEL_TOP + UI_WHEEL_RADIUS + UI_WHEEL_MARGIN;
    const int below = center_y + UI_WHEEL_RADIUS + UI_WHEEL_MARGIN + 4;
    float angle = ui_wheel_angle(ui_state.input.steering, ui_state.rotation_degrees);
    wchar_t text[UI_TEXT_CAPACITY];

    ui_draw_wheel(dc, width / 2, center_y, angle);
    _snwprintf(text, UI_TEXT_CAPACITY, L"%+.0f°", angle);
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_text_at(dc, ui_font_angle, UI_COLOR_TEXT, text, 0, below, width, below + 34, DT_CENTER);
    _snwprintf(text, UI_TEXT_CAPACITY, L"rotation %u°", ui_state.rotation_degrees);
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_text_at(dc, ui_font_label, UI_COLOR_MUTED, text, 0, below + 34, width, below + 52, DT_CENTER);
}

static void ui_draw_pedals(HDC dc, int width)
{
    const int count = (int)(sizeof(ui_pedal_views) / sizeof(ui_pedal_views[0]));
    const int gap = (width - 2 * UI_PADDING - count * UI_PEDAL_WIDTH) / (count - 1);
    const int bar_top = UI_PEDALS_TOP + UI_SECTION_GAP + 22;
    const int bar_bottom = bar_top + UI_PEDAL_HEIGHT;
    wchar_t text[UI_TEXT_CAPACITY];
    int index;

    ui_text_at(dc, ui_font_title, UI_COLOR_MUTED, L"PEDALS", UI_PADDING, UI_PEDALS_TOP, width, UI_PEDALS_TOP + 18,
               DT_LEFT);
    for (index = 0; index < count; index++) {
        const ui_pedal_view *view = &ui_pedal_views[index];
        int left = UI_PADDING + index * (UI_PEDAL_WIDTH + gap);
        int percent = ui_percent(ui_state.input.pedals[view->pedal]);
        int filled = UI_PEDAL_HEIGHT * percent / 100;

        _snwprintf(text, UI_TEXT_CAPACITY, L"%d%%", percent);
        text[UI_TEXT_CAPACITY - 1] = L'\0';
        ui_text_at(dc, ui_font_label, percent > 0 ? UI_COLOR_TEXT : UI_COLOR_MUTED, text, left - 12,
                   bar_top - 22, left + UI_PEDAL_WIDTH + 12, bar_top - 4, DT_CENTER);
        ui_fill_round(dc, left, bar_top, left + UI_PEDAL_WIDTH, bar_bottom, UI_PEDAL_RADIUS, UI_COLOR_TRACK);
        if (filled > 0)
            ui_fill_round(dc, left, bar_bottom - filled, left + UI_PEDAL_WIDTH, bar_bottom, UI_PEDAL_RADIUS,
                          view->color);
        ui_text_at(dc, ui_font_chip, UI_COLOR_MUTED, view->label, left - gap / 2, bar_bottom + 6,
                   left + UI_PEDAL_WIDTH + gap / 2, bar_bottom + 24, DT_CENTER);
    }
}

static void ui_draw_gear(HDC dc)
{
    const int top = UI_LOWER_TOP + UI_SECTION_GAP;
    wchar_t label[UI_GEAR_LABEL_CAPACITY];

    ui_text_at(dc, ui_font_title, UI_COLOR_MUTED, L"GEAR", UI_PADDING, UI_LOWER_TOP, UI_PADDING + UI_GEAR_SIZE,
               UI_LOWER_TOP + 18, DT_LEFT);
    ui_fill_round(dc, UI_PADDING, top, UI_PADDING + UI_GEAR_SIZE, top + UI_GEAR_SIZE, UI_CORNER, UI_COLOR_TRACK);
    ui_gear_label(ui_state.input.gear, ui_state.has_pattern_shifter, label);
    ui_text_at(dc, ui_font_gear, ui_state.input.gear != 0 ? UI_COLOR_ACCENT : UI_COLOR_TEXT, label, UI_PADDING,
               top, UI_PADDING + UI_GEAR_SIZE, top + UI_GEAR_SIZE, DT_CENTER | DT_VCENTER);
}

static void ui_draw_buttons(HDC dc)
{
    const int count = (int)(sizeof(ui_chip_views) / sizeof(ui_chip_views[0]));
    const int left = 2 * UI_PADDING + UI_GEAR_SIZE;
    const int top = UI_LOWER_TOP + UI_SECTION_GAP;
    int index;

    ui_text_at(dc, ui_font_title, UI_COLOR_MUTED, L"BUTTONS", left, UI_LOWER_TOP, left + 200, UI_LOWER_TOP + 18,
               DT_LEFT);
    for (index = 0; index < count; index++) {
        const ui_chip_view *view = &ui_chip_views[index];
        int x = left + (index % UI_CHIP_COLUMNS) * (UI_CHIP_WIDTH + UI_CHIP_GAP);
        int y = top + (index / UI_CHIP_COLUMNS) * (UI_CHIP_HEIGHT + UI_CHIP_GAP);
        int pressed = (ui_state.input.buttons >> view->button) & 1u;

        ui_fill_round(dc, x, y, x + UI_CHIP_WIDTH, y + UI_CHIP_HEIGHT, UI_CHIP_RADIUS,
                      pressed ? UI_COLOR_ACCENT : UI_COLOR_TRACK);
        ui_text_at(dc, ui_font_chip, pressed ? UI_COLOR_TEXT : UI_COLOR_MUTED, view->label, x, y,
                   x + UI_CHIP_WIDTH, y + UI_CHIP_HEIGHT, DT_CENTER | DT_VCENTER);
    }
}

static void ui_paint(HWND window)
{
    PAINTSTRUCT paint;
    RECT client;
    HDC screen = BeginPaint(window, &paint);
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    HBRUSH background = CreateSolidBrush(UI_COLOR_BACKGROUND);
    int width;
    int height;

    GetClientRect(window, &client);
    width = client.right;
    height = client.bottom;
    dc = CreateCompatibleDC(screen);
    bitmap = CreateCompatibleBitmap(screen, width, height);
    old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, background);
    DeleteObject(background);
    ui_fill_round(dc, 0, 0, width, height, UI_CORNER * 2, UI_COLOR_CARD);
    SetBkMode(dc, TRANSPARENT);

    ui_draw_header(dc, width);
    ui_draw_steering(dc, width);
    ui_draw_pedals(dc, width);
    ui_draw_gear(dc);
    ui_draw_buttons(dc);

    BitBlt(screen, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static LRESULT CALLBACK ui_preview_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ui_paint(window);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

HWND ui_preview_create(HWND parent, HINSTANCE instance, int x, int y, int width, int height)
{
    WNDCLASSW window_class;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = ui_preview_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    window_class.lpszClassName = UI_PREVIEW_CLASS;
    RegisterClassW(&window_class);
    ui_font_title = ui_make_font(UI_FONT_TITLE, FW_SEMIBOLD, UI_FONT_FACE_STRONG);
    ui_font_name = ui_make_font(UI_FONT_NAME, FW_SEMIBOLD, UI_FONT_FACE_STRONG);
    ui_font_label = ui_make_font(UI_FONT_LABEL, FW_NORMAL, UI_FONT_FACE);
    ui_font_angle = ui_make_font(UI_FONT_ANGLE, FW_BOLD, UI_FONT_FACE_NUMBERS);
    ui_font_gear = ui_make_font(UI_FONT_GEAR, FW_BOLD, UI_FONT_FACE_NUMBERS);
    ui_font_chip = ui_make_font(UI_FONT_CHIP, FW_SEMIBOLD, UI_FONT_FACE_STRONG);
    ZeroMemory(&ui_state, sizeof(ui_state));
    return CreateWindowExW(0, UI_PREVIEW_CLASS, L"", WS_CHILD | WS_VISIBLE, x, y, width, height, parent, NULL,
                           instance, NULL);
}

void ui_preview_update(HWND preview, const ui_preview_state *state)
{
    if (memcmp(&ui_state, state, sizeof(ui_state)) == 0)
        return;
    ui_state = *state;
    InvalidateRect(preview, NULL, FALSE);
}
