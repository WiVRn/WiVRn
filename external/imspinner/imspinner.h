#ifndef _IMSPINNER_H_
#define _IMSPINNER_H_

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021-2022 Dalerank
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copie or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */

#include <functional>
#include <array>
#include <vector>
#include <cmath>
#include <map>
#include <cctype>
#include <algorithm>

#ifdef __has_include
    #if !__has_include(<imgui.h>)
        #error "Couldn't find imgui.h in the header include path, please add it to the path!"
    #endif // !<imgui.h>
#endif // __has_include

// imgui headers
#include "imgui.h"
#include "imgui_internal.h"

namespace ImSpinner
{
    static const ImColor white{1.f, 1.f, 1.f, 1.f};
    static const ImColor half_white{1.f, 1.f, 1.f, 0.5f};
    static const ImColor red{1.f,0.f,0.f,1.f};

#define DECLPROP(name, type, def) \
    struct name { \
      type value = def; \
      operator type() { return value; } \
      name(const type& v) : value(v) {} \
    };

    enum SpinnerTypeT {
      e_st_rainbow = 0,
      e_st_angle,
      e_st_dots,
      e_st_ang,
      e_st_vdots,
      e_st_bounce_ball,
      e_st_eclipse,
      e_st_ingyang,
      e_st_barchartsine,

      e_st_count
    };

    using float_ptr = float *;
    constexpr float PI_DIV_4 = IM_PI / 4.f;
    constexpr float PI_DIV_2 = IM_PI / 2.f;
    constexpr float PI_2 = IM_PI * 2.f;
    template<class T> constexpr float PI_DIV(T d) { return IM_PI / (float)d; }
    template<class T> constexpr float PI_2_DIV(T d) { return PI_2 / (float)d; }

    DECLPROP (SpinnerType, SpinnerTypeT, e_st_rainbow)
    DECLPROP (Radius, float, 16.f)
    DECLPROP (Speed, float, 1.f)
    DECLPROP (Thickness, float, 1.f)
    DECLPROP (Color, ImColor, white)
    DECLPROP (BgColor, ImColor, white)
    DECLPROP (AltColor, ImColor, white)
    DECLPROP (Angle, float, IM_PI)
    DECLPROP (AngleMin, float, IM_PI)
    DECLPROP (AngleMax, float, IM_PI)
    DECLPROP (FloatPtr, float_ptr, nullptr)
    DECLPROP (Dots, int, 0)
    DECLPROP (MiddleDots, int, 0)
    DECLPROP (MinThickness, float, 0.f)
    DECLPROP (Reverse, bool, false)
    DECLPROP (Delta, float, 0.f)
    DECLPROP (Mode, int, 0)
#undef DECLPROP

    namespace detail {
      // SpinnerBegin is a function that starts a spinner widget, used to display an animation indicating that
      // a task is in progress. It returns true if the widget is visible and can be used, or false if it should be skipped.
      inline bool SpinnerBegin(const char *label, float radius, ImVec2 &pos, ImVec2 &size, ImVec2 &centre, int &num_segments) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
          return false;

        ImGuiContext &g = *GImGui;
        const ImGuiStyle &style = g.Style;
        const ImGuiID id = window->GetID(label);

        pos = window->DC.CursorPos;
        // The size of the spinner is set to twice the radius, plus some padding based on the style
        size = ImVec2((radius) * 2, (radius + style.FramePadding.y) * 2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(bb, style.FramePadding.y);

        num_segments = window->DrawList->_CalcCircleAutoSegmentCount(radius);

        centre = bb.GetCenter();
        // If the item cannot be added to the window, return false
        if (!ImGui::ItemAdd(bb, id))
          return false;

        return true;
      }

#define IMPLRPOP(basetype,type) basetype m_##type; \
                                void set##type(const basetype& v) { m_##type = v;} \
                                void set(type h) { m_##type = h.value;} \
                                template<typename First, typename... Args> \
                                void set(const type& h, const Args&... args) { set##type(h.value); this->template set<Args...>(args...); }
      struct SpinnerConfig {
        SpinnerConfig() {}
        template<typename none = void> void set() {}

        template<typename... Args>
        SpinnerConfig(const Args&... args) { this->template set<Args...>(args...); }

        IMPLRPOP(SpinnerTypeT, SpinnerType)
        IMPLRPOP(float, Radius)
        IMPLRPOP(float, Speed)
        IMPLRPOP(float, Thickness)
        IMPLRPOP(ImColor, Color)
        IMPLRPOP(ImColor, BgColor)
        IMPLRPOP(ImColor, AltColor)
        IMPLRPOP(float, Angle)
        IMPLRPOP(float, AngleMin)
        IMPLRPOP(float, AngleMax)
        IMPLRPOP(float_ptr, FloatPtr)
        IMPLRPOP(int, Dots)
        IMPLRPOP(int, MiddleDots)
        IMPLRPOP(float, MinThickness)
        IMPLRPOP(bool, Reverse)
        IMPLRPOP(float, Delta)
        IMPLRPOP(int, Mode)
      };
#undef IMPLRPOP

      // Bridge our use of ImDrawList::PathStroke for newer versions of dear imgui where the thickness and flags arguments
      // are swapped without enabling obsolete functions.
      inline void PathStroke(ImDrawList* draw_list, const ImU32 col, const float thickness, const ImDrawFlags flags) {
#if IMGUI_VERSION_NUM >= 19276
        draw_list->PathStroke(col, thickness, flags);
#else
        draw_list->PathStroke(col, flags, thickness);
#endif
      }

      // Same as the above compatibility function but for ImDrawList::AddPolyline
      inline void AddPolyline(ImDrawList* draw_list, const ImVec2* points, const int num_points, const ImU32 col, const float thickness, const ImDrawFlags flags)
      {
#if IMGUI_VERSION_NUM >= 19276
        draw_list->AddPolyline(points, num_points, col, thickness, flags);
#else
        draw_list->AddPolyline(points, num_points, col, flags, thickness);
#endif
      }
    }

#define SPINNER_HEADER(pos, size, centre, num_segments) \
  ImVec2 pos, size, centre; int num_segments; \
  if (!detail::SpinnerBegin(label, radius, pos, size, centre, num_segments)) { return; }; \
  ImGuiWindow *window = ImGui::GetCurrentWindow(); \
  auto circle = [&] (const std::function<ImVec2 (int)>& point_func, ImU32 dbc, float dth) { \
    window->DrawList->PathClear(); \
    for (int i = 0; i < num_segments; i++) { \
      ImVec2 p = point_func(i); \
      window->DrawList->PathLineTo(ImVec2(centre.x + p.x, centre.y + p.y)); \
    } \
    detail::PathStroke(window->DrawList, dbc, dth, 0); \
  }
    
    inline ImColor color_alpha(ImColor c, float alpha) { c.Value.w *= alpha * ImGui::GetStyle().Alpha; return c; }

    inline float damped_spring(float mass, float stiffness, float damping, float time, float a = PI_DIV_2, float b = PI_DIV_2) {
        float omega = ImSqrt(stiffness / mass);
        float alpha = damping / (2 * mass);
        float exponent = std::exp(-alpha * time);
        float cosTerm = ImCos(omega * ImSqrt(1 - alpha * alpha) * time);
        float result = exponent * cosTerm;
        return ((result *= a) + b);
    };

    inline float damped_gravity(float limtime) {
        float time = 0.0f, initialHeight = 10.f, height = initialHeight, velocity = 0.f, prtime = 0.0f;

        while (height >= 0.0) {
            if (prtime >= limtime) { return height / 10.f; }
            time += 0.01f; prtime += 0.01f;
            height = initialHeight - 0.5f * 9.81f * time * time;
            if (height < 0.0) { initialHeight = 0.0; time = 0.0; }
        }
        return 0.f;
    }

    inline float damped_trifolium(float limtime, float a = 0.f, float b = 1.f) {
        return a * ImSin(limtime) - b * ImSin(3 * limtime);
    }

    inline float damped_inoutelastic(float t, float amplitude, float period) {
        if( t == 0 ) return 0;
        t *= 2;
        if( t == 2 ) return 1;

        float s;
        if( amplitude < 1 ) {
            amplitude = 1;
            s = period / 4;
        } else {
            s = period / (2 * IM_PI) * std::asin( 1 / amplitude );
        }

        if( t < 1 ) return -0.5f * ( amplitude * ImPow(2.0f, 10.f*(t-1.f) ) * ImSin( (t-1.f-s)*(2.f*IM_PI)/period ));
        return amplitude * ImPow( 2.0f, -10*(t-1) ) * ImSin( (t-1.f-s)*(2.f*IM_PI)/period ) * 0.5f + 1.f;
    }

    inline std::pair<float, float> damped_infinity(float t, float a) {
        return std::make_pair((a * ImCos(t)) / (1 + (powf(ImSin(t), 2.0f))),
                              (a * ImSin(t) * ImCos(t)) / (1 + (powf(ImSin(t), 2.0f))));
    };

    inline float ease_inquad(float time) { return time * time; }
    inline float ease_outquad(float time) { return time * (2.f - time); }
    inline float ease_inoutquad(float t) { if (t < 0.5f) { return 2 * t * t; } else { return -1 + (4 - 2 * t) * t; }}
    inline float ease_inoutquad(float *p) { float tr = ImMax(ImSin(p[0]) - 0.5f, 0.f) * (p[1] * 0.5f); return ease_inoutquad(tr); }
    inline float ease_outcubic(float t) { float ft = t - 1; return ft * ft * ft + 1; }
    inline float ease_inexpo(float t) { return t == 0.f ? 0.f : float(pow(2, 10 * (t - 1))); }
    inline float ease_inoutexpo(float t) { if (t == 0) return 0; if (t == 1) return 1; if (t < 0.5f) return 0.5f * powf(2, (20 * t) - 10); return 0.5f * (2 - powf(2, -20 * t + 10)); }
    inline float ease_inoutexpo(float *p) { float tr = ImMax(ImSin(p[0]) - 0.5f, 0.f) * (p[1] * 0.4f); return ease_inoutexpo(tr) * (p[1] * 0.3f); }
    inline float ease_spring(float *p) { return damped_spring(1, 10.f, 1.0f, ImSin(ImFmod(p[0], p[1])), p[2], p[3]);}
    inline float ease_gravity(float *p) { return damped_gravity(p[0]); }
    inline float ease_infinity(float *p) { return damped_infinity(p[0], p[1]).second; }
    inline float ease_inoutelastic(float *p) { return damped_inoutelastic(p[1], p[2], p[3]); }
    inline float ease_sine(float *p) { return 0.5f * (1.0f - cosf(p[0] * IM_PI)); }
    inline float ease_damping(float *p) {
        const float A = 3.14f * 2;
        const float ma = 5.0f;
        const float k = 2.1f;
        const float b = 0.09f;
        const float theta = 0.0f;
        const float w = sqrtf(k / ma);
        const float t = ImFmod(*p, 25);
        double x = A * std::exp(-b * t) * std::cos(w * t - theta);
        return x;
    }

    enum ease_mode {
        e_ease_none = 0,
        e_ease_inoutquad = 1,
        e_ease_inoutexpo = 2,
        e_ease_spring = 3,
        e_ease_gravity = 4,
        e_ease_infinity = 5,
        e_ease_elastic = 6,
        e_ease_sine = 7,
        e_ease_damping = 8,
    };

    template<typename ... Args>
    inline float ease(ease_mode mode, Args ... args) {
        static_assert((std::is_same_v<Args, float> && ...), "All arguments should be of type float");
        float params[] = {args...};
        switch (mode) {
        case e_ease_inoutquad: return ease_inoutquad(params);
        case e_ease_inoutexpo: return ease_inoutexpo(params);
        case e_ease_spring: return ease_spring(params);
        case e_ease_gravity: return ease_gravity(params);
        case e_ease_infinity: return ease_infinity(params);
        case e_ease_elastic: return ease_inoutelastic(params);
        case e_ease_sine: return ease_sine(params);
        case e_ease_damping: return ease_damping(params);
        case e_ease_none: return (0.f);
        }
        return 0.f;
    }

    /*
        const char *label: A string label for the spinner, used to identify it in ImGui.
        float radius: The radius of the spinner.
        float thickness: The thickness of the spinner's border.
        const ImColor &color: The color of the spinner.
        float speed: The speed of the spinning animation.
        float ang_min: Minimum angle of spinning.
        float ang_max: Maximum angle of spinning.
        int arcs: Number of arcs of the spinner.
    */
    inline void SpinnerRainbow(const char *label, float radius, float thickness, const ImColor &color, float speed, float ang_min = 0.f, float ang_max = PI_2, int arcs = 1, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float t = (float)ImGui::GetTime();
        const ImColor c = color_alpha(color, 1.f);
        const float start = ImAbs(ImSin(t) * (num_segments - 5));
        for (int i = 0; i < arcs; ++i)
        {
            const float rb = (radius / arcs) * (i + 1);
            const float ab = ease((ease_mode)mode, t * speed + i * PI_DIV(2) / arcs, IM_PI, 1.0f, 0.0f);
            const float a_min = ImMax(ang_min, PI_2 * ((float)start) / (float)num_segments + (IM_PI / arcs) * i) + ab;
            const float a_max = ImMin(ang_max, PI_2 * ((float)num_segments + 3 * (i + 1)) / (float)num_segments) - ab;

            circle([&] (int seg) {
                const float a = a_min + ((float)seg / (float)num_segments) * (a_max - a_min);
                const float rspeed = a + t * speed;
                return ImVec2(ImCos(rspeed) * rb, ImSin(rspeed) * rb);
            }, c, thickness);
        }
    }

    inline void SpinnerRainbowMix(const char *label, float radius, float thickness, const ImColor &color, float speed, float ang_min = 0.f, float ang_max = PI_2, int arcs = 1, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
        for (int i = 0; i < arcs; ++i)
        {
            const float rb = (radius / arcs) * (i + 1);

            const float start = ImAbs(ImSin((float)ImGui::GetTime()) * (num_segments - 5));
            const float a_min = ImMax(ang_min, PI_2 * ((float)start) / (float)num_segments + (IM_PI / arcs) * i);
            const float a_max = ImMin(ang_max, PI_2 * ((float)num_segments + 3 * (i + 1)) / (float)num_segments);
            const float koeff = mode ? (1.1f - 1.f / (i+1)) : 1.f;
            ImColor c = ImColor::HSV(out_h + i * (1.f / arcs), out_s, out_v);

            circle([&] (int i) {
                const float a =  a_min + ((float)i / (float)num_segments) * (a_max - a_min);
                const float rspeed = a + (float)ImGui::GetTime() * speed * koeff;
                return ImVec2(ImCos(rspeed) * rb, ImSin(rspeed) * rb);
            }, color_alpha(c, 1.f), thickness);
        }
    }

    // This function draws a rotating heart spinner. 
    inline void SpinnerRotatingHeart(const char *label, float radius, float thickness, const ImColor &color, float speed, float ang_min = 0.f)
    {
        // Calculate the position and size of the spinner, as well as the number of segments it will be divided into.
        SPINNER_HEADER(pos, size, centre, num_segments);

        // Calculate the start angle of the spinner based on the current time and speed.
        const float start = (float)ImGui::GetTime() * speed;

        // Modify the number of segments to ensure the heart shape is complete.
        num_segments = (num_segments * 3) / 2;

        // Create a lambda function to rotate points.
        auto rotate = [] (const ImVec2 &point, float angle) {
            const float s = ImSin(angle), c = ImCos(angle);
            return ImVec2(point.x * c - point.y * s, point.x * s + point.y * c);
        };

        // Calculate the radius of the bottom of the heart.
        const float rb = radius * ImMax(0.8f, ImSin(start * 2));
        auto scale = [rb] (float v) { return v / 16.f * rb; };

        // Draw the heart spinner by calling the circle function, passing in a lambda function that defines the shape of the heart.
        circle([&] (int i) {
            const float a = PI_2 * i / num_segments;
            const float x = (scale(16) * ImPow(ImSin(a), 3));
            const float y = -1.f * (scale(13) * ImCos(a) - scale(5) * ImCos(2 * a) - scale(2) * ImCos(3 * a) - ImCos(4 * a));
            return rotate(ImVec2(x, y), ang_min);
        }, color_alpha(color, 1.f), thickness); 
    }

    // SpinnerAng is a function that draws a spinner widget with a given angle.
    inline void SpinnerAng(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = white, float speed = 2.8f, float angle = IM_PI, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);                            // Get the position, size, centre, and number of segments of the spinner using the SPINNER_HEADER macro.
        float start = (float)ImGui::GetTime() * speed;                        // The start angle of the spinner is calculated based on the current time and the specified speed.
        float b = 0.f;
        switch (mode) {
        case 1: b = damped_gravity(ImSin(start * 1.1f)) * angle; break;
        case 2: radius = (0.8f + ImCos(start) * 0.2f) * radius; break;
        case 3: b = damped_infinity(start * 1.1f, 1.f).second; break;
        }

        auto radiusmode = [radius, mode] (float a) { switch (mode) { case 4: return damped_trifolium(a) * radius; } return radius; };
        circle([&] (int i) {                                                         // Draw the background of the spinner using the `circle` function, with the specified background color and thickness.
            const float a = start + (i * (PI_2 / (num_segments - 1)));               // Calculate the angle for each segment based on the start angle and the number of segments.
            return ImVec2(ImCos(a) * radiusmode(a), ImSin(a) * radiusmode(a));
        }, color_alpha(bg, 1.f), thickness);

        circle([&] (int i) {                                                        // Draw the spinner itself using the `circle` function, with the specified color and thickness.
            const float a = start - b + (i * angle / num_segments);
            return ImVec2(ImCos(a) * radiusmode(a), ImSin(a) * radiusmode(a));
        }, color_alpha(color, 1.f), thickness);
    }

    inline void SpinnerAng8(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = white, float speed = 2.8f, float angle = IM_PI, int mode = 0, float rkoef = 0.5f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);                            // Get the position, size, centre, and number of segments of the spinner using the SPINNER_HEADER macro.
        float start = (float)ImGui::GetTime() * speed;                        // The start angle of the spinner is calculated based on the current time and the specified speed.
        float b = 0.f, kb = 1.f;
        switch (mode) {
        case 1: b = damped_gravity(ImSin(start * 1.1f)) * angle; break;
        case 2: radius = (0.8f + ImCos(start) * 0.2f) * radius; break;
        case 3: b = damped_infinity(start * 1.1f, 1.f).second; break;
        case 4: b = ease_outquad(ImSin(start * 1.1f)) * angle; break;
        case 5: kb = 2.f; break;
        }

        auto radiusmode = [radius, mode, kb] (float a, float k) { switch (mode) { case 4: return damped_trifolium(a) * radius; } return radius * k * kb; };
        float centerx_save = centre.x;
        centre.x = centerx_save + radius * (1.f - rkoef);
        circle([&] (int i) {                                                        // Draw the spinner itself using the `circle` function, with the specified color and thickness.
            const float a = start - b + (i * angle / num_segments);
            return ImVec2(ImCos(a) * radiusmode(a, rkoef), ImSin(a) * radiusmode(a, rkoef));
        }, color_alpha(color, 1.f), thickness);

        centre.x = centerx_save - radius * rkoef;
        circle([&] (int i) {                                                        // Draw the spinner itself using the `circle` function, with the specified color and thickness.
            const float a = start - b + (i * angle / num_segments);
            return ImVec2(ImCos(-a) * radiusmode(a, 1.f - rkoef), ImSin(-a) * radiusmode(a, 1.f - rkoef));
        }, color_alpha(color, 1.f), thickness);
    }

    inline void SpinnerAngMix(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, float angle = IM_PI, int arcs = 4, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);                            // Get the position, size, centre, and number of segments of the spinner using the SPINNER_HEADER macro.

        for (int i = 0; i < arcs; ++i)
        {
            const float koeff = (1.1f - 1.f / (i+1));
            float start = (float)ImGui::GetTime() * speed * koeff;                        // The start angle of the spinner is calculated based on the current time and the specified speed.
            radius = (mode == 2) ? (0.8f + ImCos(start) * 0.2f) * radius : radius;
            const float rb = (radius / arcs) * (i + 1);
            const float b = (mode == 1) ? damped_gravity(ImSin(start * 1.1f)) * angle : 0.f;
            circle([&] (int i) {                                                        // Draw the spinner itself using the `circle` function, with the specified color and thickness.
                const float a = start - b + (i * angle / num_segments);
                return ImVec2(ImCos(a) * rb, ImSin(a) * rb);
            }, color_alpha(color, 1.f), thickness);
        }
    }

    inline void SpinnerLoadingRing(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, int segments = 5)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI);                         // Calculate the starting angle based on the current time and speed
        const float bg_angle_offset = PI_2 / num_segments - 1;

        num_segments *= 2;                                                                          // Double the number of segments for the background ringxxxxxxx
        circle([&] (int i) { 
            return ImVec2(ImCos(i * bg_angle_offset) * radius, ImSin(i * bg_angle_offset) * radius); // Draw the background ring
        }, color_alpha(bg, 1.f), thickness);

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v); // Convert the color to HSV for variation in segment colors
    
        const float start_ang = (start < PI_DIV_2) ? 0.f : (start - PI_DIV_2) * 4.f;                // Calculate the angles and delta angle for each segment
        const float angle_offset = ((start < PI_DIV_2) ? PI_2 : (PI_2 - start_ang)) / segments;
        const float delta_angle = (start < PI_DIV_2) ? ImSin(start) * angle_offset : angle_offset;
        for (int i = 0; i < segments; ++i)                                                          // Draw each segment of the loading ring
        {
            window->DrawList->PathClear();
            const float begin_ang = start_ang - PI_DIV_2 + delta_angle * i;
            ImColor c = ImColor::HSV(out_h + i * (1.f / segments * 2.f), out_s, out_v);
            window->DrawList->PathArcTo(centre, radius, begin_ang, begin_ang + delta_angle, num_segments);
            detail::PathStroke(window->DrawList, color_alpha(c, 1.f), thickness, false);
        }
    }

    inline void SpinnerClock(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime() * speed;
      const float bg_angle_offset = PI_2 / (num_segments - 1);
      
      circle([&] (int i) { return ImVec2(ImCos(i * bg_angle_offset) * radius, ImSin(i * bg_angle_offset) * radius); }, color_alpha(bg, 1.f), thickness);

      window->DrawList->AddLine(centre, ImVec2(centre.x + ImCos(start) * radius, centre.y + ImSin(start) * radius), color_alpha(color, 1.f), thickness * 2);
      window->DrawList->AddLine(centre, ImVec2(centre.x + ImCos(start * 0.5f) * radius / 2.f, centre.y + ImSin(start * 0.5f) * radius / 2.f), color_alpha(color, 1.f), thickness * 2);
    }

    inline void SpinnerPulsar(const char *label, float radius, float thickness, const ImColor &bg = half_white, float speed = 2.8f, bool sequence = true, float angle = 0.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      ImGuiStorage* storage = window->DC.StateStorage;
      const ImGuiID radiusbId = window->GetID("##radiusb");
      float radius_b = storage->GetFloat(radiusbId, 0.8f);

      const float start = (float)ImGui::GetTime() * speed;
      const float bg_angle_offset = PI_2 / (num_segments - 1);

      float start_r = ImFmod(start, PI_DIV_2);
      switch (mode) {
      case 1: start_r = damped_infinity(start_r, angle).second; break;
      }
      float radius_k = ImSin(start_r);
      float radius1 = radius_k * radius;

      circle([&] (int i) {
          return ImVec2(ImCos(i * bg_angle_offset) * radius1, ImSin(i * bg_angle_offset) * radius1);
      }, color_alpha(bg, 1.f), thickness);

      if (sequence) { radius_b -= (0.005f * speed); radius_b = ImMax(radius_k, ImMax(0.8f, radius_b)); } 
      else { radius_b = (1.f - radius_k); }
      storage->SetFloat(radiusbId, radius_b);
      
      float radius_tb = sequence ? ImMax(radius_k, radius_b) * radius : (radius_b * radius);
      circle([&] (int i) {
          return ImVec2(ImCos(i * bg_angle_offset) * radius_tb, ImSin(i * bg_angle_offset) * radius_tb);
      }, color_alpha(bg, 1.f), thickness);
    }

    inline void SpinnerDoubleFadePulsar(const char *label, float radius, float /*thickness*/, const ImColor &bg = half_white, float speed = 2.8f)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      ImGuiStorage* storage = window->DC.StateStorage;
      const ImGuiID radiusbId = window->GetID("##radiusb");
      float radius_b = storage->GetFloat(radiusbId, 0.8f);

      const float start = (float)ImGui::GetTime() * speed;
      const float bg_angle_offset = PI_2_DIV(num_segments);

      float start_r = ImFmod(start, PI_DIV_2);
      float radius_k = ImSin(start_r);
      window->DrawList->AddCircleFilled(centre, radius_k * radius, color_alpha(bg, ImMin(0.1f, radius_k)), num_segments);

      radius_b = (1.f - radius_k);
      storage->SetFloat(radiusbId, radius_b);

      window->DrawList->AddCircleFilled(centre, radius_b * radius, color_alpha(bg, ImMin(0.3f, radius_b)), num_segments);
    }

    inline void SpinnerTwinPulsar(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int rings = 2, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float bg_angle_offset = PI_2 / (num_segments - 1);
      const float koeff = PI_DIV(2 * rings);
      float start = (float)ImGui::GetTime() * speed;

      for (int num_ring = 0; num_ring < rings; ++num_ring) {
        float radius_k = ImSin(ImFmod(start + (num_ring * koeff), PI_DIV_2));
        float radius1 = radius_k * radius;
        radius1 += ease((ease_mode)mode, start, radius);

        circle([&] (int i) {
            const float a = start + (i * bg_angle_offset);
            return ImVec2(ImCos(a) * radius1, ImSin(a) * radius1);
        }, color_alpha(color, radius_k > 0.5f ? 2.f - (radius_k * 2.f) : color.Value.w), thickness);
      }
    }

    inline void SpinnerFadePulsar(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, int rings = 2, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float bg_angle_offset = PI_2_DIV(num_segments);
      const float koeff = PI_DIV(2 * rings);
      float start = (float)ImGui::GetTime() * speed;

      for (int num_ring = 0; num_ring < rings; ++num_ring) {
        float radius_k = ImSin(ImFmod(start + (num_ring * koeff), PI_DIV_2));
        ImColor c = color_alpha(color, (radius_k > 0.5f) ? (2.f - (radius_k * 2.f)) : color.Value.w);
        c.Value.w -= ease((ease_mode)mode, start, c.Value.w);
        window->DrawList->AddCircleFilled(centre, radius_k * radius, c, num_segments);
      }
    }

    inline void SpinnerFadePulsarSquare(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, int rings = 2, int mode = 0) {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float bg_angle_offset = PI_2_DIV(num_segments);
        const float koeff = PI_DIV(2 * rings);
        float start = (float)ImGui::GetTime() * speed;

        for (int num_ring = 0; num_ring < rings; ++num_ring) {
            float start_r = ImFmod(start, PI_DIV_2);
            float radius_k = ImSin(start_r * (1.f - (1.f / rings) * num_ring));
            radius_k += ease((ease_mode)mode, bg_angle_offset, speed);
            radius_k = std::clamp(radius_k, 0.f, 1.f);

            ImColor c = color_alpha(color, (radius_k > 0.5f) ? (2.f - (radius_k * 2.f)) : color.Value.w);
            c.Value.w = 0.8f / (1 + rings);//ease((ease_mode)mode, start, c.Value.w);
            float px = radius_k * radius;
            window->DrawList->AddRectFilled(ImVec2(centre.x - px, centre.y - px), ImVec2(centre.x + px, centre.y + px), c, 2.f);

            px = radius * (1.f - radius_k);
            window->DrawList->AddRectFilled(ImVec2(centre.x - px, centre.y - px), ImVec2(centre.x + px, centre.y + px), c, 2.f);
        }
    }

    inline void SpinnerCircularLines(const char *label, float radius, const ImColor &color = white, float speed = 1.8f, int lines = 8, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        auto ghalf_pi = [] (float f) -> float { return ImMin(f, PI_DIV_2); };
        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI);
        const float bg_angle_offset = PI_2_DIV(lines);
        for (size_t j = 0; j < 3; ++j)
        {
            const float start_offset = j * PI_DIV(7.f);
            const float rmax = ImMax(ImSin(ghalf_pi(start - start_offset)), 0.3f) * radius;
            const float rmin = ImMax(ImSin(ghalf_pi(start - PI_DIV_4 - start_offset)), 0.3f) * radius;

            ImColor c = color_alpha(color, 1.f - j * 0.3f);
            for (size_t i = 0; i <= lines; i++)
            {
                float a = (i * bg_angle_offset);
                a += ease((ease_mode)mode, start_offset, radius);
                window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * rmin, centre.y + ImSin(a) * rmin),
                                          ImVec2(centre.x + ImCos(a) * rmax, centre.y + ImSin(a) * rmax),
                                          color_alpha(c, 1.f), 1.f);
            }
        }
    }

    inline void SpinnerDots(const char *label, float *nextdot, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t dots = 12, float minth = -1.f, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float start = (float)ImGui::GetTime() * speed;
        const float bg_angle_offset = PI_2 / dots;
        dots = ImMin(dots, (size_t)32);
        const size_t mdots = dots / 2;

        float def_nextdot = 0;
        float &ref_nextdot = nextdot ? *nextdot : def_nextdot;
        if (ref_nextdot < 0.f)
          ref_nextdot = (float)dots;

        auto radiusmode = [radius, mode, dots] (float a, int i) { 
            switch (mode) { 
            case 2: return damped_trifolium(a) * radius;
            case 3: return (radius / dots) * i;
            } return radius; 
        };
        auto thcorrect = [&thickness, &ref_nextdot, &mdots, &minth] (size_t i) {
            const float nth = minth < 0.f ? thickness / 2.f : minth;
            return ImMax(nth, ImSin(((i - ref_nextdot) / mdots) * IM_PI) * thickness);
        };

        switch (mode) {
        case 1: start = damped_infinity(start * 1.1f, 1.f).second; break;
        case 4: start = ease_outquad(ImSin(ImFmod(start, IM_PI))); break;
        case 5: start = ease_inoutexpo(ImSin(ImFmod(start, IM_PI))); break;
        }
        for (size_t i = 0; i <= dots; i++)
        {
            float a = start + (i * bg_angle_offset);
            a = ImFmod(a, PI_2);
            float th = minth < 0 ? thickness / 2.f : minth;

            if (ref_nextdot + mdots < dots) {
                if (i > ref_nextdot && i < ref_nextdot + mdots)
                    th = thcorrect(i);
            } else {
                if ((i > ref_nextdot && i < dots) || (i < ((int)(ref_nextdot + mdots)) % dots))
                    th = thcorrect(i);
            }
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(-a) * radiusmode(a, i), centre.y + ImSin(-a) * radiusmode(a, i)), th, color_alpha(color, 1.f), 8);
        }
    }

    inline void SpinnerVDots(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bgcolor = white, float speed = 2.8f, size_t dots = 12, size_t mdots = 6, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float start = (float)ImGui::GetTime() * speed;
        switch (mode) {
        case 1: start += ease_inoutquad(ImSin(ImFmod(start, IM_PI))); break;
        }
        
        const float bg_angle_offset = PI_2_DIV(dots);
        dots = ImMin(dots, (size_t)32);

        for (size_t i = 0; i <= dots; i++)
        {
            float a = ImFmod(start + (i * bg_angle_offset), PI_2);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(-a) * radius, centre.y + ImSin(-a) * radius), thickness / 2, color_alpha(bgcolor, 1.f), 8);
        }

        window->DrawList->PathClear();
        const float d_ang = (mdots / (float)dots) * PI_2;
        const float angle_offset = (d_ang) / dots;
        for (size_t i = 0; i < dots; i++)
        {
            const float a = start + (i * angle_offset);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
        }
        detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
    }



    //const float sina = ImSin( ImFmod((start + (IM_PI - i * offset)), PI_DIV_2));






    inline void Spinner4Caleidospcope(const char *label, float radius, float thickness, const ImColor &color = 0xffffffff, float speed = 2.8f, int lt = 8)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float nextItemKoeff = 2.5f;
        const float offset = size.x / 4.f;

        float ab = start;
        int msize = 2;

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
        for (size_t i = 0; i < msize; i++)
        {
            float a = ab - i * IM_PI;
            ImColor c = color_alpha(ImColor::HSV(out_h + (0.1f * i), out_s, out_v, 0.7f), 1.f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x - offset + ImSin(a) * offset, centre.y + ImCos(a) * offset), thickness, c, lt);
        }

        for (size_t i = 0; i < msize; i++)
        {
            float a = ab + i * IM_PI + PI_DIV_2;
            ImColor c = color_alpha(ImColor::HSV(out_h + 0.2f + (0.1f * i), out_s, out_v, 0.7f), 1.f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImSin(a) * offset, centre.y - offset + ImCos(a) * offset), thickness, c, lt);
        }

        float ba = start; msize = 2;
        for (size_t i = 0; i < msize; i++)
        {
            float a = -ba + i * IM_PI + PI_DIV_2;
            ImColor c = color_alpha(ImColor::HSV(out_h + 0.4f + (0.1f * i), out_s, out_v, 0.7f), 1.f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + offset + ImSin(a) * offset, centre.y + ImCos(a) * offset), thickness, c, lt);
        }

        for (size_t i = 0; i < msize; i++)
        {
            float a = ab - i * IM_PI + PI_DIV_4;
            ImColor c = color_alpha(ImColor::HSV(out_h + 0.6f + (0.1f * i), out_s, out_v, 0.7f), 1.f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImSin(a) * offset, centre.y + offset + ImCos(a) * offset), thickness, c, lt);
        }
    }


    inline void SpinnerThickToSin(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int nt = 1, int lt = 8, int mode = 0) {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        float length = ImFmod(start, IM_PI);
        const float dangle = ImSin(length) * IM_PI * 0.5f;
        const float angle_offset = IM_PI / (lt * 2);

        auto draw_spring = [&] (float k, float r) {
            float arc = 0.f;
            size_t i = 0;
            for (; i < (lt * 2); i++) {
                float a = start + (i * angle_offset);
                a += ease((ease_mode)mode, a, dangle);
                if (ImSin(a) < 0.f)
                    a *= -1;
                arc += angle_offset;
                if (arc > dangle)
                    break;
                float th = thickness * (2 * fabs(ImCos(start)));
                th = ImMax(th, 1.f);

                window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * r, centre.y + k * ImSin(a) * r), th, color_alpha(color, 1.f), 8);
            }
        };

        for (int num_ring = 0; num_ring < nt; ++num_ring) {
            draw_spring(-1 - num_ring * 0.1, radius * (1 - 0.1 * num_ring));
        }
    }



    inline void SpinnerSquareSpins(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float nextItemKoeff = 2.5f;
        const float heightSpeed = 0.8f;
        const float dots = (size.x / (thickness * nextItemKoeff));
        const float start = (float)ImGui::GetTime() * speed;

        for (size_t i = 0; i < dots; i++)
        {
            const float a = ImFmod(start + i * ((PI_DIV_2 * 0.7f) / dots), PI_DIV_2);
            const float th = thickness * (ImCos(a * heightSpeed) * 2.f);
            ImVec2 pmin = ImVec2(centre.x - (size.x / 2.f) + i * thickness * nextItemKoeff - thickness, centre.y - thickness);
            ImVec2 pmax = ImVec2(centre.x - (size.x / 2.f) + i * thickness * nextItemKoeff + thickness, centre.y + thickness);
            window->DrawList->AddRect(pmin, pmax, color_alpha(color, 1.f), 0.f);
            ImVec2 lmin = ImVec2(centre.x - (size.x / 2.f) + i * thickness * nextItemKoeff - thickness, centre.y - th + thickness);
            ImVec2 lmax = ImVec2(centre.x - (size.x / 2.f) + i * thickness * nextItemKoeff + thickness - 1, centre.y - th + thickness);
            window->DrawList->AddLine(lmin, lmax, color_alpha(color, 1.f), 1.f);
        }
    }





    inline void SpinnerTwinAng(const char *label, float radius1, float radius2, float thickness, const ImColor &color1 = white, const ImColor &color2 = red, float speed = 2.8f, float angle = IM_PI, int mode = 0)
    {
      const float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      const float aoffset = ImFmod((float)ImGui::GetTime(), 1.5f * IM_PI);
      const float bofsset = (aoffset > angle) ? angle : aoffset;
      const float angle_offset = angle * 2.f / num_segments;

      window->DrawList->PathClear();
      for (size_t i = 0; i <= 2 * num_segments; i++)
      {
        float b = ease((ease_mode)mode, start + i * PI_DIV(2) / num_segments, IM_PI, 1.0f, 0.0f);
        const float a = start + b + (i * angle_offset);
        if (i * angle_offset > 2 * bofsset)
          break;
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1));
      }
      detail::PathStroke(window->DrawList, color_alpha(color1, 1.f), thickness, false);

      window->DrawList->PathClear();
      for (size_t i = 0; i < num_segments / 2; i++)
      {
        float b = ease((ease_mode)mode, start + i * PI_DIV(2) / num_segments, IM_PI, 1.0f, 0.0f);
        const float a = start - b + (i * angle_offset);
        if (i * angle_offset > bofsset)
          break;
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius2, centre.y + ImSin(a) * radius2));
      }
      detail::PathStroke(window->DrawList, color_alpha(color2, 1.f), thickness, false);
    }

    inline void SpinnerFilling(const char *label, float radius, float thickness, const ImColor &color1 = white, const ImColor &color2 = red, float speed = 2.8f)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      const float angle_offset = PI_2_DIV(num_segments - 1);

      circle([&] (int i) {
          const float a = (i * angle_offset);
          return ImVec2(ImCos(a) * radius, ImSin(a) * radius);
      }, color_alpha(color1, 1.f), thickness);

      window->DrawList->PathClear();
      for (size_t i = 0; i < 2 * num_segments / 2; i++)
      {
        const float a = (i * angle_offset);
        if (a > start)
          break;
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
      }
      detail::PathStroke(window->DrawList, color_alpha(color2, 1.f), thickness, false);
    }

    inline void SpinnerFillingMem(const char *label, float radius, float thickness, const ImColor &color, ImColor &colorbg, float speed)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float angle_offset = PI_2_DIV(num_segments - 1);
        num_segments *= 4;

        circle([&] (int i) {
            const float a = (i * angle_offset);
            return ImVec2(ImCos(a) * radius, ImSin(a) * radius);
        }, color_alpha(colorbg, 1.f), thickness);

        if (start < 0.02f) {
            colorbg = color;
        }

        window->DrawList->PathClear();
        for (size_t i = 0; i < 2 * num_segments / 2; i++) {
            const float a = (i * angle_offset);
            if (a > start)
                break;
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
        }
        detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
    }

    inline void SpinnerTopup(const char *label, float radius1, float radius2, const ImColor &color = red, const ImColor &fg = white, const ImColor &bg = white, float speed = 2.8f)
    {
      const float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI);
      window->DrawList->AddCircleFilled(centre, radius1, color_alpha(bg, 1.f), num_segments);

      const float abegin = (PI_DIV_2) - start;
      const float aend = (PI_DIV_2) + start;
      const float angle_offset = (aend - abegin) / num_segments;

      window->DrawList->PathClear();
      window->DrawList->PathArcTo(centre, radius1, abegin, aend, num_segments * 2);

      ImDrawListFlags save = window->DrawList->Flags;
      window->DrawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        
      window->DrawList->PathFillConvex(color_alpha(color, 1.f));

      window->DrawList->AddCircleFilled(centre, radius2, color_alpha(fg, 1.f), num_segments);

      window->DrawList->Flags = save;
    }

    inline void SpinnerTwinAng180(const char *label, float radius1, float radius2, float thickness, const ImColor &color1 = white, const ImColor &color2 = red, float speed = 2.8f, float angle = PI_DIV_4, int mode = 0)
    {
      const float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      num_segments *= 8;
      const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      const float aoffset = ImFmod((float)ImGui::GetTime(), PI_2);
      const float bofsset = (aoffset > IM_PI) ? IM_PI : aoffset;
      const float angle_offset = PI_2_DIV(num_segments);
      float ared_min = 0, ared = 0;
      if (aoffset > IM_PI)
        ared_min = aoffset - IM_PI;

      window->DrawList->PathClear();
      auto radiusmode = [mode] (float a, float r, float f) { switch (mode) { case 2: return damped_trifolium(a, 0.f, f) * r; } return r; };
      for (size_t i = 0; i <= num_segments / 2 + 1; i++)
      {
        ared = start + (i * angle_offset);
        switch (mode) {
        case 1: ared += damped_infinity(start, angle).second; break;
        }

        if (i * angle_offset < ared_min)
          continue;

        if (i * angle_offset > bofsset)
          break;

        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ared) * radiusmode(ared, radius2, -1.1f), centre.y + ImSin(ared) * radiusmode(ared, radius2, -1.1f)));
      }
      detail::PathStroke(window->DrawList, color_alpha(color2, 1.f), thickness, false);

      window->DrawList->PathClear();
      for (size_t i = 0; i <= 2 * num_segments + 1; i++)
      {
        const float a = ared + ared_min + (i * angle_offset);
        if (i * angle_offset < ared_min)
          continue;

        if (i * angle_offset > bofsset)
          break;

        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radiusmode(a, radius1, 1.f), centre.y + ImSin(a) * radiusmode(a, radius1, 1.f)));
      }
      detail::PathStroke(window->DrawList, color_alpha(color1, 1.f), thickness, false);
    }

    inline void SpinnerTwinAng360(const char *label, float radius1, float radius2, float thickness, const ImColor &color1 = white, const ImColor &color2 = red, float speed1 = 2.8f, float speed2 = 2.5f, int mode = 0)
    {
      const float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      num_segments *= 4;
      float start1 = ImFmod((float)ImGui::GetTime() * speed1, PI_2);
      float start2 = ImFmod((float)ImGui::GetTime() * speed2, PI_2);
      const float aoffset = ImFmod((float)ImGui::GetTime(), 2.f * IM_PI);
      const float bofsset = (aoffset > IM_PI) ? IM_PI : aoffset;
      const float angle_offset = PI_2 / num_segments;
      float ared_min = 0, ared = 0;
      if (aoffset > IM_PI)
        ared_min = aoffset - IM_PI;

      window->DrawList->PathClear();
      for (size_t i = 0; i <= num_segments + 1; i++) {
        ared = ( mode ? damped_spring(1, 10.f, 1.0f, ImSin(ImFmod(start1 + 0 * PI_DIV(2), PI_2))) : start1) + (i * angle_offset);
        if (i * angle_offset < ared_min * 2) continue;
        if (i * angle_offset > bofsset * 2.f) break;
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ared) * radius2, centre.y + ImSin(ared) * radius2));
      }
      detail::PathStroke(window->DrawList, color_alpha(color2, 1.f), thickness, false);

      window->DrawList->PathClear();
      for (size_t i = 0; i <= num_segments + 1; i++) {
        ared = (mode ? damped_spring(1, 10.f, 1.0f, ImSin(ImFmod(start2 + 1 * PI_DIV(2), PI_2))) : start2) + (i * angle_offset);
        if (i * angle_offset < ared_min * 2) continue;
        if (i * angle_offset > bofsset * 2.f) break;
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(-ared) * radius1, centre.y + ImSin(-ared) * radius1));
      }
      detail::PathStroke(window->DrawList, color_alpha(color1, 1.f), thickness, false);
    }




    inline void SpinnerFadeTris(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, size_t dim = 2, bool scale = false, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        ImGuiContext &g = *GImGui;
        const ImGuiStyle &style = g.Style;
        const float nextItemKoeff = 1.5f;
        const float yOffsetKoeftt = 0.8f;
        const float heightSpeed = 0.8f;
        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);

        std::vector<ImVec2> points;
        auto pushPoints = [] (std::vector<ImVec2> &pp, const ImVec2 &p1, const ImVec2 &p2, const ImVec2 &p3) { pp.push_back(p1); pp.push_back(p2); pp.push_back(p3); };
        auto hsumPoints = [] (const ImVec2 &p1, const ImVec2 &p2) { return ImVec2((p1.x + p2.x) / 2.f, (p1.y + p2.y) / 2.f); };

        auto splitTriangle = [&] (const ImVec2 &p1, const ImVec2 &p2, const ImVec2 &p3, int numDivisions) {
            pushPoints(points, p1, p2, p3);

            for (int i = 0; i < numDivisions; i++) {
                std::vector<ImVec2> newPoints;
                for (int j = 0; j < points.size() - 2; j += 3) {
                    ImVec2 p1 = points[j];
                    ImVec2 p2 = points[j + 1];
                    ImVec2 p3 = points[j + 2];

                    ImVec2 p4((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
                    ImVec2 p5((p2.x + p3.x) / 2, (p2.y + p3.y) / 2);
                    ImVec2 p6((p3.x + p1.x) / 2, (p3.y + p1.y) / 2);

                    pushPoints(newPoints, p1, p4, p6);
                    pushPoints(newPoints, p4, p5, p6);
                    pushPoints(newPoints, p4, p2, p5);
                    pushPoints(newPoints, p6, p5, p3);
                }
                points = newPoints;
            }

            return points;
        };

        auto calculateAngle = [] (ImVec2 v1, ImVec2 v2, const ImVec2 c) {
            v1.x -= c.x; v1.y -= c.y; v2.x -= c.x; v2.y -= c.y;
            float dotProduct = v1.x * v2.x + v1.y * v2.y;
            float magnitudeV1 = ImSqrt(v1.x * v1.x + v1.y * v1.y);
            float magnitudeV2 = ImSqrt(v2.x * v2.x + v2.y * v2.y);
            float angleInRadians = ImAcos(dotProduct / (magnitudeV1 * magnitudeV2));
            float crossProduct = v1.x * v2.y - v2.x * v1.y;
            float signedAngle = std::copysign(angleInRadians, crossProduct);
            return fmod(signedAngle + PI_2, PI_2);
        };

        const float offset = IM_PI / dim;
        ImVec2 p1 = ImVec2(centre.x + ImSin(0) * radius, centre.y + ImCos(0) * radius);
        ImVec2 p2 = ImVec2(centre.x + ImSin(PI_DIV(3) * 2) * radius, centre.y + ImCos(PI_DIV(3) * 2) * radius);
        ImVec2 p3 = ImVec2(centre.x + ImSin(PI_DIV(3) * 4) * radius, centre.y + ImCos(PI_DIV(3) * 4) * radius);
        std::vector<ImVec2> subdividedPoints = splitTriangle(p1, p2, p3, dim);
        for (size_t i = 0; i < subdividedPoints.size(); i+=3) {
            ImVec2 trisCenter = hsumPoints(hsumPoints(subdividedPoints[i], subdividedPoints[i + 1]), subdividedPoints[i + 2]);
            const float angle = calculateAngle(p1, trisCenter, centre);
            ImColor c = color_alpha(color, 1.f - ImMax(0.1f, ImFmod(start + angle, PI_2) / PI_2));
            window->DrawList->AddTriangleFilled(subdividedPoints[i], subdividedPoints[i+1], subdividedPoints[i+2], c);
        }
    }



    inline void SpinnerAngTwin(const char *label, float radius1, float radius2, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, float angle = IM_PI, size_t arcs = 1, int mode = 0)
    {
      float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      float start = (float)ImGui::GetTime()* speed;
      const float bg_angle_offset = PI_2 / num_segments;

      window->DrawList->PathClear();
      for (size_t i = 0; i <= num_segments; i++) {
        const float a = start + (i * bg_angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1));
      }
      detail::PathStroke(window->DrawList, color_alpha(bg, 1.f), thickness, false);

      const float angle_offset = angle / num_segments;
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num) {
          window->DrawList->PathClear();
          float arc_start = 2 * IM_PI / arcs;
          float b = ease((ease_mode)mode, start + arc_num * PI_DIV(2) / arcs, IM_PI, 1.0f, 0.0f);
          //switch (mode) {
          //case 1: b = start + damped_spring(1, 10.f, 1.0f, , )), 1, 0); break;
          //case 2: b = start + damped_infinity(PI_2 - angle, start).second; break;
          //default: b = start;
          //}
          
          for (size_t i = 0; i < num_segments; i++) {
            const float a = start + b + arc_start * arc_num + (i * angle_offset);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius2, centre.y + ImSin(a) * radius2));
          }
          detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
      }
    }

    inline void SpinnerArcRotation(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num) {
        window->DrawList->PathClear();
        ImColor c = color_alpha(color, ImMax(0.1f, arc_num / (float)arcs));
        float b = ease((ease_mode)mode, start + arc_num * PI_DIV(2) / arcs, IM_PI, 1.0f, 0.0f);
        for (size_t i = 0; i <= num_segments; i++) {
          const float a = start + b + arc_angle * arc_num + (i * angle_offset);
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
        }
        detail::PathStroke(window->DrawList, c, thickness, false);
      }
    }

    inline void SpinnerArcFade(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime()* speed, IM_PI * 4.f);
      const float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
      {
        window->DrawList->PathClear();
        for (size_t i = 0; i <= num_segments + 1; i++)
        {
          const float a = arc_angle * arc_num + (i * angle_offset) - PI_DIV_2 - PI_DIV_4;
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
        }
        const float a = arc_angle * arc_num;
        ImColor c = color;
        if (start < PI_2) {
          c.Value.w = 0.f;
          if (start > a && start < (a + arc_angle)) { c.Value.w = 1.f - (start - a) / (float)arc_angle; }
          else if (start < a) { c.Value.w = 1.f; }
          float woff = ease((ease_mode)mode, start - a, 4.f); 
          c.Value.w = ImMax(0.05f, 1.f - c.Value.w - woff);
        } else {
          const float startk = start - PI_2;
          c.Value.w = 0.f;
          if (startk > a && startk < (a + arc_angle)) { c.Value.w = 1.f - (startk - a) / (float)arc_angle; }
          else if (startk < a) { c.Value.w = 1.f; }
          float woff = ease((ease_mode)mode, start - a, 4.f); 
          c.Value.w = ImMax(0.05f, c.Value.w + woff);
        }

        detail::PathStroke(window->DrawList, color_alpha(c, 1.f), thickness, false);
      }
    }

    inline void SpinnerSimpleArcFade(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)     {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI * 4.f);
        const float arc_angle = PI_2 / (float)4;
        const float angle_offset = arc_angle / num_segments;

        auto draw_segment = [&] (int arc_num, float delta, auto c, float k, float t) {
            window->DrawList->PathClear();
            for (size_t i = 0; i <= num_segments + 1; i++) {
                const float a = t * start + arc_angle * arc_num + (i * angle_offset) - PI_DIV_2 - PI_DIV_4 + delta;
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius * k, centre.y + ImSin(a) * radius * k));
            }
            detail::PathStroke(window->DrawList, color_alpha(c, 1.f), thickness, false);
        };

        for (size_t arc_num = 0; arc_num < 2; ++arc_num) {
            const float a = arc_angle * arc_num;
            ImColor c = color;
            if (start < PI_2) {
                c.Value.w = 0.f;
                if (start > a && start < (a + arc_angle)) { c.Value.w = 1.f - (start - a) / (float)arc_angle; }
                else if (start < a) { c.Value.w = 1.f; }
                c.Value.w = ImMax(0.05f, 1.f - c.Value.w);
            } else {
                const float startk = start - PI_2;
                c.Value.w = 0.f;
                if (startk > a && startk < (a + arc_angle)) { c.Value.w = 1.f - (startk - a) / (float)arc_angle; }
                else if (startk < a) { c.Value.w = 1.f; }
                c.Value.w = ImMax(0.05f, c.Value.w);
            }

            draw_segment(arc_num, 0.f, c, 1.f + arc_num * 0.3f, arc_num > 0 ? -1 : 1);
            draw_segment(arc_num, IM_PI, c, 1.f + arc_num * 0.3f, arc_num > 0 ? -1 : 1);
        }
    }

    inline void SpinnerSquareStrokeFade(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI * 4.f);
        const float arc_angle = PI_DIV_2;
        const float ht = thickness / 2.f;

        for (size_t arc_num = 0; arc_num < 4; ++arc_num)
        {
            float a = arc_angle * arc_num;
            ImColor c = color_alpha(color, 1.f);
            if (start < PI_2) {
                c.Value.w = (start > a && start < (a + arc_angle))
                                ? 1.f - (start - a) / (float)arc_angle
                                : (start < a ? 1.f : 0.f);
                c.Value.w = ImMax(0.05f, 1.f - c.Value.w);
            } else {
                const float startk = start - PI_2;
                c.Value.w = (startk > a && startk < (a + arc_angle))
                                ? 1.f - (startk - a) / (float)arc_angle
                                : (startk < a ? 1.f : 0.f);
                c.Value.w = ImMax(0.05f, c.Value.w);
            }
            a -= PI_DIV_4;
            const float r = radius * 1.4f;
            const bool right = ImSin(a) > 0;
            const bool top = ImCos(a) < 0;
            ImVec2 p1(centre.x + ImCos(a) * r, centre.y + ImSin(a) * r);
            ImVec2 p2(centre.x + ImCos(a - PI_DIV_2) * r, centre.y + ImSin(a - PI_DIV_2) * r);
            switch (arc_num) {
            case 0: p1.x -= ht; p2.x -= ht; break;
            case 1: p1.y -= ht; p2.y -= ht; break;
            case 2: p1.x += ht; p2.x += ht; break;
            case 3: p1.y += ht; p2.y += ht; break;
            }
            window->DrawList->AddLine(p1, p2, color_alpha(c, 1.f), thickness);
        }
    }

#if IMGUI_VERSION_NUM < 19197
    #define IMSPINNER_FIND_GLYPH(x) Font->FindGlyph(x)
#else
    #define IMSPINNER_FIND_GLYPH(x) FontBaked->FindGlyph(x)
#endif


    inline void SpinnerAsciiSymbolPoints(const char *label, const char* text, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        if (!text || !*text)
            return;

        const float start = ImFmod((float)ImGui::GetTime() * speed, (float)strlen(text));
        const ImFontGlyph* glyph = ImGui::GetCurrentContext()->IMSPINNER_FIND_GLYPH(text[(int)start]);

        ImVec2 pp(centre.x - radius, centre.y - radius);
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        unsigned char* bitmap;
        int out_width, out_height;

#if IMGUI_VERSION_NUM < 19197
        atlas->GetTexDataAsAlpha8(&bitmap, &out_width, &out_height);
#else
        const auto* atlas_tex_data = atlas->TexData;
        out_width = atlas_tex_data->Width;
        out_height = atlas_tex_data->Height;
        bitmap = atlas_tex_data->Pixels;
#endif

        const int U1 = (int)(glyph->U1 * out_width);
        const int U0 = (int)(glyph->U0 * out_width);
        const int V1 = (int)(glyph->V1 * out_height);
        const int V0 = (int)(glyph->V0 * out_height);
        const float px = size.x / (U1 - U0);
        const float py = size.y / (V1 - V0);
        
        for (int x = U0, ppx = 0; x < U1; x++, ppx++) {
            for (int y = V0, ppy = 0; y < V1; y++, ppy++) {
                ImVec2 point(pp.x + (ppx * px), pp.y + (ppy * py));
                // * 4 + 3 because dear imgui now defaults to RGBA32
                const unsigned char alpha = bitmap[(out_width * y + x)
#if IMGUI_VERSION_NUM > 19197
                                                                        * 4 + 3];
#else
                                                                               ];
#endif
                window->DrawList->AddCircleFilled(point, thickness * 1.5f, color_alpha({.5f,.5f,.5f,.5f}, alpha / 255.f));
                window->DrawList->AddCircleFilled(point, thickness, color_alpha(color, alpha / 255.f));
            }
        }
    }

    inline void SpinnerSevenSegments(const char *label, const char* text, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        if (!text || !*text)
            return;

        const float start = ImFmod((float)ImGui::GetTime() * speed, (float)strlen(text));

        struct Segment { ImVec2 b, e; };
        const float q = 1.f, hq = q * 0.5f, xq = thickness / radius;
        const Segment segments[] = {{ ImVec2{-hq, 0.0f}, ImVec2{hq, 0.0f} }, /*central*/
                                     { ImVec2{-hq - xq, 0.0f}, ImVec2{-hq - xq, -q} }, /*left up*/
                                     { ImVec2{-hq - xq, q}, ImVec2{-hq - xq, xq} }, /*left down*/
                                     { ImVec2{-hq, q}, ImVec2{hq, q} }, /*center up*/
                                     { ImVec2{hq + xq, q}, ImVec2{hq + xq, xq} }, /*right down*/
                                     { ImVec2{hq + xq, 0.0f}, ImVec2{hq + xq, -q} }, /*right up*/
                                     { ImVec2{-hq, -q}, ImVec2{hq, -q} } /*center down*/};

        const int symbols[][8]{
            //segments
            //g,f,e,d,c,b,a,sym
            // NUMBERS
            {0,1,1,1,1,1,1,0}, //ZERO
            {0,0,0,0,1,1,0,1}, //ONE
            {1,0,1,1,0,1,1,2}, //TWO
            {1,0,0,1,1,1,1,3}, //THREE
            {1,1,0,0,1,1,0,4}, //FOUR
            {1,1,0,1,1,0,1,5}, //FIVE
            {1,1,1,1,1,0,1,6}, //SIX
            {0,0,0,0,1,1,1,7}, //SEVEN
            {1,1,1,1,1,1,1,8}, //EIGHT
            {1,1,0,1,1,1,1,9}, //NINE
            // LETTERS
            {1,1,1,0,1,1,1,'A'}, //LETTER A
            {1,1,1,1,1,0,0,'B'}, //LETTER B
            {0,1,1,1,0,0,1,'C'}, //LETTER C
            {1,0,1,1,1,1,0,'D'}, //LETTER D
            {1,1,1,1,0,0,1,'E'}, //LETTER E
            {1,1,1,0,0,0,1,'F'}, //LETTER F
            {0,1,1,1,1,0,1,'G'}, //LETTER G
            {1,1,1,0,1,0,0,'H'}, //LETTER H
            {0,1,1,0,0,0,0,'I'}, //LETTER I
            {0,0,1,1,1,1,0,'J'}, //LETTER J
            {1,1,1,0,1,0,1,'K'}, //LETTER K
            {0,1,1,1,0,0,0,'L'}, //LETTER L
            {0,0,1,0,1,0,1,'M'}, //LETTER M
            {0,1,1,0,1,1,1,'N'}, //LETTER N
            {0,1,1,1,1,1,1,'O'}, //LETTER O
            {1,1,1,0,0,1,1,'P'}, //LETTER P
            {1,1,0,0,1,1,1,'Q'}, //LETTER Q
            {0,1,1,0,0,1,1,'R'}, //LETTER R
            {1,1,0,1,1,0,1,'S'}, //LETTER S
            {1,1,1,1,0,0,0,'T'}, //LETTER T
            {0,1,1,1,1,1,0,'U'}, //LETTER U
            {0,1,0,1,1,1,0,'V'}, //LETTER V
            {0,1,0,1,0,1,0,'W'}, //LETTER W
            {1,1,1,0,1,1,0,'X'}, //LETTER X
            {1,1,0,1,1,1,0,'Y'}, //LETTER Y
            {1,0,0,1,0,1,1,'Z'}, //LETTER Z
        };

        auto draw_segment = [&] (const Segment &s) {
            ImVec2 p1(centre.x + radius * s.b.x, centre.y + radius * s.b.y);
            ImVec2 p2(centre.x + radius * s.e.x, centre.y + radius * s.e.y);

            window->DrawList->AddLine(p1, p2, color_alpha(color, 1.f), thickness);
        };

        auto draw_symbol = [&] (const int* sq) {
            for (int i = 0; i < 7; ++i)
                sq[i] ? draw_segment(segments[i]) : void();
        };
        char current_char = text[(int)start];
        if (isalpha(current_char)) {
            draw_symbol(symbols[tolower(current_char) - 'a' + 10]);
        } else if (isdigit(current_char)) {
            draw_symbol(symbols[current_char - '0']);
        }
    }

    inline void SpinnerSquareStrokeFill(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float overt = 3.f;
        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2 + overt);
        const float arc_angle = 2.f * PI_DIV_4;
        const float ht = thickness / 2.f;

        const float r = radius * 1.4f;
        ImVec2 pp(centre.x + ImCos(-IM_PI * 0.75f) * r, centre.y + ImSin(-IM_PI * 0.75f) * r);
        if (start > PI_2) {
            ImColor c = color;
            const float delta = (start - PI_2) / overt;
            if (delta < 0.5f) {
                c.Value.w = 1.f - delta * 2.f;
                window->DrawList->AddLine(ImVec2(pp.x - ht, pp.y), ImVec2(pp.x + ht, pp.y), color_alpha(c, 1.f), thickness);
            } else {
                c.Value.w = (delta - 0.5f) * 2.f;
                window->DrawList->AddLine(ImVec2(pp.x - ht, pp.y), ImVec2(pp.x + ht, pp.y), color_alpha(color, 1.f), thickness);
            }
        } else {
            window->DrawList->AddLine(ImVec2(pp.x - ht, pp.y), ImVec2(pp.x + ht, pp.y), color_alpha(color, 1.f), thickness);
        }

        if (start < PI_2) {
            for (size_t arc_num = 0; arc_num < 4; ++arc_num) {
                float a = arc_angle * arc_num;
                float segment_progress = (start > a && start < (a + arc_angle))
                    ? 1.f - (start - a) / (float)arc_angle
                    : (start < a ? 1.f : 0.f);
                a -= PI_DIV_4;
                segment_progress = 1.f - segment_progress;
                ImVec2 p1(centre.x + ImCos(a - PI_DIV_2) * r, centre.y + ImSin(a - PI_DIV_2) * r);
                ImVec2 p2(centre.x + ImCos(a) * r, centre.y + ImSin(a) * r);
                switch (arc_num) {
                case 0: p1.x += ht; p2.x -= ht; p2.x = p1.x + (p2.x - p1.x) * segment_progress; break;
                case 1: p1.y -= ht; p2.y -= ht; p2.y = p1.y + (p2.y - p1.y) * segment_progress; break;
                case 2: p1.x += ht; p2.x += ht; p2.x = p1.x + (p2.x - p1.x) * segment_progress; break;
                case 3: p1.y += ht; p2.y -= ht; p2.y = p1.y + (p2.y - p1.y) * segment_progress; break;
                }
                window->DrawList->AddLine(p1, p2, color_alpha(color, 1.f), thickness);
            }
        }
    }

    inline void SpinnerSquareStrokeLoading(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2 );
        const float arc_angle = 2.f * PI_DIV_4;
        const float ht = thickness / 2.f;

        const float best_radius = radius * 1.4f;
        const float delta = (start - PI_2) / 2.f;
        for (size_t arc_num = 0; arc_num < 4; ++arc_num) {
            float a = arc_angle * arc_num;
            a -= PI_DIV_4;
            ImVec2 pp(centre.x + ImCos(a) * best_radius, centre.y + ImSin(a) * best_radius);
            window->DrawList->AddLine(ImVec2(pp.x - ht, pp.y), ImVec2(pp.x + ht, pp.y), color_alpha(color, 1.f), thickness);
        }

        const bool grow = start < IM_PI;
        const float segment_progress = grow ? (start / IM_PI) : (1.f - (start - IM_PI) / IM_PI);
        for (size_t arc_num = 0; arc_num < 4; ++arc_num)             {
            float a = arc_angle * arc_num;
            a -= PI_DIV_4;
            const bool right = ImSin(a) > 0;
            const bool top = ImCos(a) < 0;
            ImVec2 p1(centre.x + ImCos(a - PI_DIV_2) * best_radius, centre.y + ImSin(a - PI_DIV_2) * best_radius);
            ImVec2 p2(centre.x + ImCos(a) * best_radius, centre.y + ImSin(a) * best_radius);
            switch (arc_num) {
            case 0: p1.x -= ht; p2.x += ht;
                p1.x = grow ? p1.x : p1.x + (p2.x - p1.x) * (1.f - segment_progress); p2.x = grow ? p1.x + (p2.x - p1.x) * segment_progress : p2.x; break;
            case 1: p1.y -= ht; p2.y += ht;
                p1.y = grow ? p1.y : p1.y + (p2.y - p1.y) * (1.f - segment_progress); p2.y = grow ? p1.y + (p2.y - p1.y) * segment_progress : p2.y; break;
            case 2: p1.x += ht; p2.x -= ht;
                p1.x = grow ? p1.x : grow ? p1.x : p1.x + (p2.x - p1.x) * (1.f - segment_progress); p2.x = grow ? p1.x + (p2.x - p1.x) * segment_progress : p2.x; break;
            case 3: p1.y += ht; p2.y -= ht;
                p1.y = grow ? p1.y : p1.y + (p2.y - p1.y) * (1.f - segment_progress); p2.y = grow ? p1.y + (p2.y - p1.y) * segment_progress : p2.y; break;
            }
            window->DrawList->AddLine(p1, p2, color_alpha(color, 1.f), thickness);
        }
    }

    inline void SpinnerSquareLoading(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2 + PI_DIV_2 );
        const float arc_angle = PI_DIV_2;
        const float ht = thickness / 2.f;

        const float best_radius = radius * 1.4f;
        float a = arc_angle * 3 - PI_DIV_4 + (start > PI_2 ? start * 2.f : 0);
        ImVec2 last_pos(centre.x + ImCos(a) * best_radius, centre.y + ImSin(a) * best_radius);
        ImVec2 ppMin, ppMax;
        for (size_t arc_num = 0; arc_num < 4; ++arc_num) {              
            a = arc_angle * arc_num - PI_DIV_4 + (start > PI_2 ? start * 2.f : 0);
            ImVec2 pp(centre.x + ImCos(a) * best_radius, centre.y + ImSin(a) * best_radius);
            window->DrawList->AddLine(last_pos, pp, color_alpha(color, 1.f), thickness);
            last_pos = pp;

            if (start < PI_2) {
                if (arc_num == 2) ppMin = ImVec2(centre.x + ImCos(a) * best_radius * 0.8f, centre.y + ImSin(a) * best_radius * 0.8f);
                else if (arc_num == 0) ppMax = ImVec2(centre.x + ImCos(a) * best_radius * 0.8f, centre.y + ImSin(a) * best_radius * 0.8f);
            }
        }

        if (start < PI_2) {
            ppMax.y = ppMin.y + (start / PI_2) * (ppMax.y - ppMin.y);
            window->DrawList->AddRectFilled(ppMin, ppMax, color_alpha(color, 1.f), 0.f);
        }
    }

    inline void SpinnerFilledArcFade(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime()* speed, IM_PI * 4.f);
      const float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
      {
        const float b = arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
        const float e = arc_angle * arc_num + arc_angle - PI_DIV_2 - PI_DIV_4;
        const float a = arc_angle * arc_num;
        ImColor c = color;
        float vradius = radius;
        if (start < PI_2) {
          c.Value.w = 0.f;
          if (start > a && start < (a + arc_angle)) { c.Value.w = 1.f - (start - a) / (float)arc_angle; }
          else if (start < a) { c.Value.w = 1.f; }
          c.Value.w = ImMax(0.f, 1.f - c.Value.w);
          if (mode == 1)
            vradius = radius * c.Value.w;
        }
        else
        {
          const float startk = start - PI_2;
          c.Value.w = 0.f;
          if (startk > a && startk < (a + arc_angle)) { c.Value.w = 1.f - (startk - a) / (float)arc_angle; }
          else if (startk < a) { c.Value.w = 1.f; }
          if (mode == 1)
            vradius = radius * c.Value.w;
        }

        window->DrawList->PathClear();
        window->DrawList->PathLineTo(centre);
        for (size_t i = 0; i <= num_segments + 1; i++)
        {
          const float ar = arc_angle * arc_num + (i * angle_offset) - PI_DIV_2 - PI_DIV_4;
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar) * vradius, centre.y + ImSin(ar) * vradius));
        }
        window->DrawList->PathFillConvex(color_alpha(c, 1.f));
      }
    }

    inline void SpinnerPointsRoller(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t points = 8, int circles = 2, float rspeed = 1.f) {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI / (points / 2));
        const float arc_angle = PI_2 / (float)points;
        const float angle_offset = arc_angle / num_segments;

        float dspeed = rspeed;
        float angleStep = IM_PI * 2.0f / points; // Step between dots

        for (int c_num = 0; c_num < circles; c_num++) {
            float vradius = radius * (1.f - (1.f / (circles + 2.f) * c_num));
            float adv_angle = ((IM_PI * 2) / circles) * c_num;// *(1.f + (0.1f * circles) * c_num);
            for (size_t arc_num = 0; arc_num < points; ++arc_num) {
                const float b = arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
                const float e = arc_angle * arc_num + arc_angle - PI_DIV_2 - PI_DIV_4;
                const float a = arc_angle * arc_num;

                float angle = angleStep * arc_num + start * speed; // Calculate angle for each dot
                float alpha = 1.0f - (angle / (IM_PI * 2.0f)); // Fade effect

                ImU32 dotColor = ImGui::GetColorU32(ImVec4(
                    (color >> IM_COL32_R_SHIFT) / 255.0f,
                    (color >> IM_COL32_G_SHIFT) / 255.0f,
                    (color >> IM_COL32_B_SHIFT) / 255.0f,
                    alpha
                ));

                const float ar = start + adv_angle + arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
                window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(ar) * vradius, centre.y + ImSin(ar) * vradius), thickness, color_alpha(dotColor, 1.f), 8);
            }
            dspeed += rspeed;
        }
    }

    inline void SpinnerPointsArcBounce(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t points = 4, int circles = 2, float rspeed = 0.f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime()* speed, IM_PI * 4.f);
        const float arc_angle = PI_2 / (float)points;
        const float angle_offset = arc_angle / num_segments;
        float dspeed = rspeed;
        for (int c_num = 0; c_num < circles; c_num++)
        {
            float mr = radius * (1.f - (1.f / (circles + 2.f) * c_num));
            float adv_angle = IM_PI * c_num;// *(1.f + (0.1f * circles) * c_num);
            for (size_t arc_num = 0; arc_num < points; ++arc_num)
            {
                const float b = arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
                const float e = arc_angle * arc_num + arc_angle - PI_DIV_2 - PI_DIV_4;
                const float a = arc_angle * arc_num;
                ImColor c = color;
                float vradius = mr;
                if (start < PI_2) {
                    c.Value.w = 0.f;
                    if (start > a && start < (a + arc_angle)) { c.Value.w = 1.f - (start - a) / (float)arc_angle; }
                    else if (start < a) { c.Value.w = 1.f; }
                    c.Value.w = ImMax(0.f, 1.f - c.Value.w);
                     vradius = mr * c.Value.w;
                }
                else
                {
                    const float startk = start - PI_2;
                    c.Value.w = 0.f;
                    if (startk > a && startk < (a + arc_angle)) { c.Value.w = 1.f - (startk - a) / (float)arc_angle; }
                    else if (startk < a) { c.Value.w = 1.f; }
                    vradius = mr * c.Value.w;
                }

                const float ar = start * dspeed + adv_angle + arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
                window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(ar) * vradius, centre.y + ImSin(ar) * vradius), thickness, color_alpha(c, 1.f), 8);
            }
            dspeed += rspeed;
        }
    }

    inline void SpinnerFilledArcColor(const char *label, float radius, const ImColor &color = red, const ImColor &bg = white, float speed = 2.8f, size_t arcs = 4)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime()* speed, PI_2);
      const float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;

      window->DrawList->AddCircleFilled(centre, radius, color_alpha(bg, 1.f), num_segments * 2);
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
      {
        const float b = arc_angle * arc_num - PI_DIV_2;
        const float e = arc_angle * arc_num + arc_angle - PI_DIV_2;
        const float a = arc_angle * arc_num;

        ImColor c = color;
        c.Value.w = 0.f;
        if (start > a && start < (a + arc_angle)) { c.Value.w = 1.f - (start - a) / (float)arc_angle; }
        else if (start < a) { c.Value.w = 1.f; }
        c.Value.w = ImMax(0.f, 1.f - c.Value.w);
        
        window->DrawList->PathClear();
        window->DrawList->PathLineTo(centre);
        for (size_t i = 0; i < num_segments + 1; i++)
        {
          const float ar = arc_angle * arc_num + (i * angle_offset) - PI_DIV_2;
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar) * radius, centre.y + ImSin(ar) * radius));
        }
        window->DrawList->PathFillConvex(color_alpha(c, 1.f));
      }
    }

    inline void SpinnerFilledArcRing(const char *label, float radius, float thickness, const ImColor &color = red, const ImColor &bg = white, float speed = 2.8f, size_t arcs = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float pi_div_2 = PI_DIV_2;
        const float pi_div_4 = PI_DIV_4;
        const float pi_mul_2 = PI_2;
        const float start = ImFmod((float)ImGui::GetTime() * speed, pi_mul_2 + pi_div_4);
        const float arc_angle = pi_mul_2 / (float)arcs;
        const float angle_offset = arc_angle / num_segments;

        window->DrawList->PathClear();
        for (size_t i = 0; i < num_segments + 1; i++)
        {
            const float ar_b = (i * (pi_mul_2 / num_segments));
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar_b) * radius, centre.y + ImSin(ar_b) * radius));
        }
        detail::PathStroke(window->DrawList, color_alpha(bg, 1.f), thickness, false);

        for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
        {
            const float b = arc_angle * arc_num - pi_div_2;
            const float e = arc_angle * arc_num + arc_angle - pi_div_2;
            const float a = arc_angle * arc_num;

            float alpha = 0.f;
            if (start > pi_mul_2) { alpha = (start - pi_mul_2) / pi_div_4; }
            else if (start > a && start < (a + arc_angle)) { alpha = 1.f - (start - a) / (float)arc_angle; }
            else if (start < a) { alpha = 1.f; }

            window->DrawList->PathClear();
            for (size_t i = 0; i < num_segments + 1; i++)
            {
                const float ar_b = arc_angle * arc_num + (i * angle_offset) - pi_div_2;
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar_b) * radius, centre.y + ImSin(ar_b) * radius));
            }
            detail::PathStroke(window->DrawList, color_alpha(color, ImMax(0.f, 1.f - alpha)), thickness, false);
        }
    }

    inline void SpinnerArcWedges(const char *label, float radius, const ImColor &color = red, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime() * speed;
        const float arc_angle = PI_2 / (float)arcs;
        const float angle_offset = arc_angle / num_segments;
        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);

        for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
        {
            const float b = arc_angle * arc_num - PI_DIV_2;
            const float e = arc_angle * arc_num + arc_angle - PI_DIV_2;
            const float a = arc_angle * arc_num;

            window->DrawList->PathClear();
            window->DrawList->PathLineTo(centre);
            const float ab = ease((ease_mode)mode, start + arc_num * PI_DIV(2) / arcs, IM_PI, 1.0f, 0.0f);
            for (size_t i = 0; i < num_segments + 1; i++)
            {
                const float start_a = ImFmod(start * (1.05f * (arc_num + 1)), PI_2);
                const float ar = start_a + ab + arc_angle * arc_num + (i * angle_offset) - PI_DIV_2;
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar) * radius, centre.y + ImSin(ar) * radius));
            }
            window->DrawList->PathFillConvex(color_alpha(ImColor::HSV(out_h + (1.f / arcs) * arc_num, out_s, out_v, 0.7f), 1.f));
        }
    }

    inline void SpinnerTwinBall(const char *label, float radius1, float radius2, float thickness, float b_thickness, const ImColor &ball = white, const ImColor &bg = half_white, float speed = 2.8f, size_t balls = 2, int mode = 0)
    {
      float radius = ImMax(radius1, radius2);
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float bg_angle_offset = PI_2 / num_segments;

      window->DrawList->PathClear();
      for (size_t i = 0; i <= num_segments; i++)
      {
        const float a = start + (i * bg_angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1));
      }
      detail::PathStroke(window->DrawList, color_alpha(bg, 1.f), thickness, false);

      for (size_t b_num = 0; b_num < balls; ++b_num)
      {
        float b_start = PI_2 / balls;
        const float ab = ease((ease_mode)mode, start + b_num * PI_DIV(2) / balls, IM_PI, 1.0f, 0.0f);
        const float a = b_start * b_num + start + ab;
        window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * radius2, centre.y + ImSin(a) * radius2), b_thickness, color_alpha(ball, 1.f));
      }
    }

    inline void SpinnerSolarBalls(const char *label, float radius, float thickness, const ImColor &ball = white, const ImColor &bg = half_white, float speed = 2.8f, size_t balls = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime() * speed;
        const float bg_angle_offset = PI_2 / num_segments;

        for (int i = 0; i < balls; ++i) {
            const float rb = (radius / balls) * 1.3f * (i + 1);
            window->DrawList->AddCircle(centre, rb, color_alpha(bg, 1.f), num_segments, thickness * 0.3f);
        }

        for (int i = 0; i < balls; ++i) {
            const float rb = (radius / balls) * 1.3f * (i + 1);
            const float a = start * (1.0f + 0.1f * i);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * rb, centre.y + ImSin(a) * rb), thickness, color_alpha(ball, 1.f));
        }
    }

    inline void SpinnerSolarScaleBalls(const char *label, float radius, float thickness, const ImColor &ball = white, float speed = 2.8f, size_t balls = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI * 16.f);
        const float bg_angle_offset = PI_2 / num_segments;

        for (int i = 0; i < balls; ++i) {
            const float rb = (radius / balls) * 1.3f * (i + 1);
            const float a = start * (1.0f + 0.1f * i);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * rb, centre.y + ImSin(a) * rb), ((thickness * 2.f) / balls) * i, color_alpha(ball, 1.f));
        }
    }

    inline void SpinnerSolarArcs(const char *label, float radius, float thickness, const ImColor &ball = white, const ImColor &bg = half_white, float speed = 2.8f, size_t balls = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime()* speed;
        const int half_segments = num_segments / 2;

        for (int i = 0; i < balls; ++i)
        {
            const float rb = (radius / balls) * 1.3f * (i + 1);
            const float bg_angle_offset = IM_PI / half_segments;
            const int mul = i % 2 ? -1 : 1;
            window->DrawList->PathClear();
            for (size_t ii = 0; ii <= half_segments; ii++)
            {
                const float a = (ii * bg_angle_offset);
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * rb, centre.y + ImSin(a) * rb * mul));
            }
            detail::PathStroke(window->DrawList, color_alpha(bg, 1.f), thickness * 0.8f, false);
        }

        for (int i = 0; i < balls; ++i)
        {
            const float rb = (radius / balls) * 1.3f * (i + 1);
            const float a = ImFmod(start * (1.0f + 0.1f * i), PI_2);
            const int mul = i % 2 ? -1 : 1;
            float y = ImSin(a) * rb;
            if ((y > 0 && mul < 0) || (y < 0 && mul > 0)) y = -y;
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * rb, centre.y + y), thickness, color_alpha(ball, 1.f));
        }
    }

    inline void SpinnerMovingArcs(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImFmod(ImGui::GetTime() * speed, IM_PI * 2);
        const int half_segments = num_segments / 2;

        for (int i = 0; i < arcs; ++i) {
            const float rb = (radius / arcs) * 1.3f * (i + 1);
            float a = damped_spring(1, 10.f, 1.0f, ImSin(ImFmod(start + i * PI_DIV(arcs), PI_2)));
            const float angle = ImMax(PI_DIV_2, (1.f - i/(float)arcs) * IM_PI);
            circle([&] (int i) {
                const float b = a + (i * angle / num_segments);
                return ImVec2(ImCos(b) * rb, ImSin(b) * rb);
            }, color_alpha(color, 1.f), thickness);
        }
    }

    inline void SpinnerRainbowCircle(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, float mode = 1)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime() * speed;
        num_segments *= 2;
        const float bg_angle_offset = IM_PI / num_segments;

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);

        for (int i = 0; i < arcs; ++i)
        {
            float max_angle = IM_PI - ImFmod(start /* * (1.0 + 0.1f * i) */, IM_PI + PI_DIV_2 + PI_DIV(8));
            max_angle = ImMin(IM_PI, max_angle + (PI_DIV_2 / arcs) * i);
            const float rb = (radius / arcs) * 1.1f * (i + 1);

            ImColor c = ImColor::HSV(out_h + i * (0.8f / arcs), out_s, out_v);
            const int draw_segments = ImMax<int>(0, (int)(max_angle / bg_angle_offset));

            for (int j = 0; j < 2; j++)
            {
                const int mul = j % 2 ? -1 : 1;
                const float py = (j % 2 ? -0.5f : 0.5f) * thickness;
                const float alpha_start = (j % 2 ? 0 : IM_PI) * mode;
                window->DrawList->PathClear();
                for (size_t ii = 0; ii <= draw_segments + 1; ii++)
                {
                    const float a = (ii * bg_angle_offset);
                    window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(IM_PI - alpha_start - a) * rb, centre.y + ImSin(a) * rb * mul + py));
                }
                detail::PathStroke(window->DrawList, color_alpha(c, 1.f), thickness * 0.8f, false);
            }
        }
    }

    inline void SpinnerBounceBall(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int dots = 1, bool shadow = false)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      ImGuiStorage* storage = window->DC.StateStorage;
      const ImGuiID vtimeId = window->GetID("##vtime");
      const ImGuiID hmaxId = window->GetID("##hmax");

      float vtime = storage->GetFloat(vtimeId, 0.f);
      float hmax = storage->GetFloat(hmaxId, 1.f);

      vtime += 0.05f;
      hmax += 0.01f;

      storage->SetFloat(vtimeId, vtime);
      storage->SetFloat(hmaxId, hmax);

      constexpr float rkoeff[9] = {0.1f, 0.15f, 0.17f, 0.25f, 0.31f, 0.19f, 0.08f, 0.24f, 0.9f};
      const int iterations = shadow ? 4 : 1;
      for (int j = 0; j < iterations; j++) {
          ImColor c = color_alpha(color, 1.f - 0.15f * j);
          for (int i = 0; i < dots; i++) {
              float start = ImFmod((float)ImGui::GetTime() * speed * (1 + rkoeff[i % 9]) - (IM_PI / 12.f) * j, IM_PI);
              float sign = ((i % 2 == 0) ? 1.f : -1.f);
              float offset = (i == 0) ? 0.f : (floorf((i+1) / 2.f + 0.1f) * sign * 2.f * thickness);
              float maxht = damped_gravity(ImSin(ImFmod(hmax, IM_PI))) * radius;
              window->DrawList->AddCircleFilled(ImVec2(centre.x + offset, centre.y + radius - ImSin(start) * 2.f * maxht), thickness, c, 8);
          }
      }
    }

    inline void SpinnerPulsarBall(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, bool shadow = false, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        ImGuiStorage* storage = window->DC.StateStorage;

        const int iterations = shadow ? 4 : 1;
        for (int j = 0; j < iterations; j++) {
            ImColor c = color_alpha(color, 1.f - 0.15f * j);
            float start = ImFmod((float)ImGui::GetTime() * speed - (IM_PI / 12.f) * j, IM_PI);
            float maxht = damped_gravity(ImSin(ImFmod(start, IM_PI))) * (radius * 0.6f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x, centre.y), maxht, c, num_segments);
        }

        const float angle_offset = PI_DIV_2 / num_segments;
        const int arcs = 2;
        for (size_t arc_num = 0; arc_num < arcs; ++arc_num) {
            window->DrawList->PathClear();
            float arc_start = 2 * IM_PI / arcs;
            float start = ImFmod((float)ImGui::GetTime() * speed - (IM_PI * arc_num), IM_PI);
            float b = mode ? start + damped_spring(1, 10.f, 1.0f, ImSin(ImFmod(start + arc_num * PI_DIV(2) / arcs, IM_PI)), 1, 0) : start;
            float maxht = (damped_gravity(ImSin(ImFmod(start, IM_PI))) * 0.3f + 0.7f) * radius;
            for (size_t i = 0; i < num_segments; i++) {
                const float a = b + arc_start * arc_num + (i * angle_offset);
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * maxht, centre.y + ImSin(a) * maxht));
            }
            detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
        }
    }



    inline void SpinnerAngTriple(const char *label, float radius1, float radius2, float radius3, float thickness, const ImColor &c1 = white, const ImColor &c2 = half_white, const ImColor &c3 = white, float speed = 2.8f, float angle = IM_PI)
    {
      float radius = ImMax(ImMax(radius1, radius2), radius3);
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start1 = (float)ImGui::GetTime() * speed;
      const float angle_offset = angle / num_segments;

      window->DrawList->PathClear();
      for (size_t i = 0; i < num_segments; i++)
      {
        const float a = start1 + (i * angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1));
      }
      detail::PathStroke(window->DrawList, color_alpha(c1, 1.f), thickness, false);

      float start2 = (float)ImGui::GetTime() * 1.2f * speed;
      window->DrawList->PathClear();
      for (size_t i = 0; i < num_segments; i++)
      {
        const float a = start2 + (i * angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(-a) * radius2, centre.y + ImSin(-a) * radius2));
      }
      detail::PathStroke(window->DrawList, color_alpha(c2, 1.f), thickness, false);

      float start3 = (float)ImGui::GetTime() * 0.9f * speed;
      window->DrawList->PathClear();
      for (size_t i = 0; i < num_segments; i++)
      {
        const float a = start3 + (i * angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius3, centre.y + ImSin(a) * radius3));
      }
      detail::PathStroke(window->DrawList, color_alpha(c3, 1.f), thickness, false);
    }

    inline void SpinnerAngEclipse(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, float angle = IM_PI)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float angle_offset = angle / num_segments;
      const float th = thickness / num_segments;

      for (size_t i = 0; i < num_segments; i++)
      {
        const float a = start + (i * angle_offset);
        const float a1 = start + ((i+1) * angle_offset);
        window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius),
                                  ImVec2(centre.x + ImCos(a1) * radius, centre.y + ImSin(a1) * radius),
                                  color_alpha(color, 1.f),
                                  th * i);
      }
    }

    inline void SpinnerIngYang(const char *label, float radius, float thickness, bool reverse, float yang_detlta_r, const ImColor &colorI = white, const ImColor &colorY = white, float speed = 2.8f, float angle = IM_PI * 0.7f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float startI = (float)ImGui::GetTime() * speed;
      const float startY = (float)ImGui::GetTime() * (speed + (yang_detlta_r > 0.f ? ImClamp(yang_detlta_r * 0.5f, 0.5f, 2.f) : 0.f));
      const float angle_offset = angle / num_segments;
      const float th = thickness / num_segments;

      for (int i = 0; i < num_segments; i++)
      {
        const float ab = ease((ease_mode)mode, startI + i * PI_DIV(2) / num_segments, IM_PI, 1.0f, 0.0f);
        const float a = startI + ab + (i * angle_offset);
        const float a1 = startI + ab + ((i + 1) * angle_offset);
        window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius),
                                  ImVec2(centre.x + ImCos(a1) * radius, centre.y + ImSin(a1) * radius),
                                  color_alpha(colorI, 1.f),
                                  th * i);
      }
      const float ab = ease((ease_mode)mode, startI + PI_DIV(2), IM_PI, 1.0f, 0.0f);
      const float ai_end = startI + ab + (num_segments * angle_offset);
      ImVec2 circle_i_center{centre.x + ImCos(ai_end) * radius, centre.y + ImSin(ai_end) * radius};
      window->DrawList->AddCircleFilled(circle_i_center, thickness / 2.f, color_alpha(colorI, 1.f), num_segments);

      const float rv = reverse ? -1.f : 1.f;
      const float yang_radius = (radius - yang_detlta_r);
      for (int i = 0; i < num_segments; i++)
      {
        const float ae = ease((ease_mode)mode, startI + i * PI_DIV(2) / num_segments, IM_PI, 1.0f, 0.0f);
        const float a = startY - ae + IM_PI + (i * angle_offset);
        const float a1 = startY - ae + IM_PI + ((i+1) * angle_offset);
        window->DrawList->AddLine(ImVec2(centre.x + ImCos(a * rv) * yang_radius, centre.y + ImSin(a * rv) * yang_radius),
                                  ImVec2(centre.x + ImCos(a1 * rv) * yang_radius, centre.y + ImSin(a1 * rv) * yang_radius),
                                  color_alpha(colorY, 1.f),
                                  th * i);
      }
      const float ae = ease((ease_mode)mode, startI + PI_DIV(2), IM_PI, 1.0f, 0.0f);
      const float ay_end = startY - ae + IM_PI + (num_segments * angle_offset);
      ImVec2 circle_y_center{centre.x + ImCos(ay_end * rv) * yang_radius, centre.y + ImSin(ay_end * rv) * yang_radius};
      window->DrawList->AddCircleFilled(circle_y_center, thickness / 2.f, color_alpha(colorY, 1.f), num_segments);
    }


    inline void SpinnerGooeyBalls(const char *label, float radius, const ImColor &color, float speed, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float start = ImFmod((float)ImGui::GetTime() * speed, IM_PI);
      start = mode ? damped_spring(1, 10.f, 1.0f, ImSin(start), 1, 0) : start;
      const float radius1 = (0.4f + 0.3f * ImSin(start)) * radius;
      const float radius2 = radius - radius1;

      window->DrawList->AddCircleFilled(ImVec2(centre.x - radius + radius1, centre.y), radius1, color_alpha(color, 1.f), num_segments);
      window->DrawList->AddCircleFilled(ImVec2(centre.x - radius + radius1 * 1.2f + radius2, centre.y), radius2, color_alpha(color, 1.f), num_segments);
    }


    inline void SpinnerRotateGooeyBalls(const char *label, float radius, float thickness, const ImColor &color, float speed, int balls, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
      const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      float radius1 = (0.2f + 0.3f * ImSin(start)) * radius;
      const float angle_offset = PI_2 / balls;

      float roff = ease((ease_mode)mode, start, radius);
      for (int i = 0; i <= balls; i++)
      {
        const float a = rstart + (i * angle_offset);
        window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * (radius1 + roff), centre.y + ImSin(a) * (radius1 + roff)), thickness, color_alpha(color, 1.f), num_segments);
      }
    }

    inline void SpinnerHerbertBalls(const char *label, float radius, float thickness, const ImColor &color, float speed, int balls)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
        const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float radius1 = 0.3f * radius;
        const float radius2 = 0.8f * radius;
        const float angle_offset = PI_2 / balls;

        for (int i = 0; i < balls; i++)
        {
            const float a = rstart + (i * angle_offset);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1), thickness, color_alpha(color, 1.f), num_segments);
        }

        for (int i = 0; i < balls * 2; i++)
        {
            const float a = -rstart + (i * angle_offset / 2.f);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * radius2, centre.y + ImSin(a) * radius2), thickness, color_alpha(color, 1.f), num_segments);
        }
    }

    inline void SpinnerHerbertBalls3D(const char *label, float radius, float thickness, const ImColor &color, float speed)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
        const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float radius1 = 0.3f * radius;
        const float radius2 = 0.8f * radius;
        const int balls = 2;
        const float angle_offset = PI_2 / balls;

        ImVec2 frontpos, backpos;
        for (int i = 0; i < balls; i++)
        {
            const float a = rstart + (i * angle_offset);
            const float t = (i == 1 ? 0.7f : 1.f) * thickness;
            const ImVec2 pos = ImVec2(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1);
            window->DrawList->AddCircleFilled(pos, t, color_alpha(color, 1.f), num_segments);
            if (i == 0) frontpos = pos; else backpos = pos;
        }

        ImVec2 lastpos;
        for (int i = 0; i <= balls * 2; i++)
        {
            const float a = -rstart + (i * angle_offset / 2.f);
            const ImVec2 pos = ImVec2(centre.x + ImCos(a) * radius2, centre.y + ImSin(a) * radius2);
            float t = sqrt(pow(pos.x - frontpos.x, 2) + pow(pos.y - frontpos.y, 2)) / (radius * 1.f) * thickness;
            window->DrawList->AddCircleFilled(pos, t, color_alpha(color, 1.f), num_segments);
            window->DrawList->AddLine(pos, backpos, color_alpha(color, 0.5f), ImMax(thickness / 2.f, 1.f));
            if (i > 0) {
              window->DrawList->AddLine(pos, lastpos, color_alpha(color, 1.f), ImMax(thickness / 2.f, 1.f));
            }
            window->DrawList->AddLine(pos, frontpos, color_alpha(color, 1.f), ImMax(thickness / 2.f, 1.f));
            lastpos = pos;
        }
    }

    inline void SpinnerRotateTriangles(const char *label, float radius, float thickness, const ImColor &color, float speed, int tris, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
      const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      const float radius1 = radius / 2.5f + thickness;
      const float angle_offset = PI_2 / tris;

      for (int i = 0; i <= tris; i++)
      {
        const float ab = ease((ease_mode)mode, start + i * PI_DIV(2) / tris, IM_PI, 1.0f, 0.0f);
        const float a = rstart + ab + (i * angle_offset);
        ImVec2 tri_centre(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1);
        ImVec2 p1(tri_centre.x + ImCos(-a) * radius1, tri_centre.y + ImSin(-a) * radius1);
        ImVec2 p2(tri_centre.x + ImCos(-a + PI_2 / 3.f) * radius1, tri_centre.y + ImSin(-a + PI_2 / 3.f) * radius1);
        ImVec2 p3(tri_centre.x + ImCos(-a - PI_2 / 3.f) * radius1, tri_centre.y + ImSin(-a - PI_2 / 3.f) * radius1);
        ImVec2 points[] = {p1, p2, p3};
        window->DrawList->AddConvexPolyFilled(points, 3, color_alpha(color, 1.f));
      }
    }

    inline void SpinnerRotateShapes(const char *label, float radius, float thickness, const ImColor &color, float speed, int shapes, int pnt)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
        const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float radius1 = radius / 2.5f + thickness;
        const float angle_offset = PI_2 / shapes;

        std::vector<ImVec2> points(pnt);
        const float begin_a = -IM_PI / ((pnt % 2 == 0) ? pnt : (pnt - 1));
        for (int i = 0; i <= shapes; i++)
        {
            const float a = rstart + (i * angle_offset);
            ImVec2 tri_centre(centre.x + ImCos(a) * radius1, centre.y + ImSin(a) * radius1);
            for (int pi = 0; pi < pnt; ++pi) {
                points[pi] = {tri_centre.x + ImCos(begin_a + pi * PI_2 / pnt) * radius1, tri_centre.y + ImSin(begin_a + pi * PI_2 / pnt) * radius1};
            }
            window->DrawList->AddConvexPolyFilled(points.data(), pnt, color_alpha(color, 1.f));
        }
    }

    inline void SpinnerSinSquares(const char *label, float radius, float thickness, const ImColor &color, float speed, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime(), IM_PI);
        const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float radius1 = radius / 2.5f + thickness;
        const float angle_offset = PI_DIV_2;

        std::vector<ImVec2> points(4);
        for (int i = 0; i <= 4; i++)
        {
            const float a = rstart + (i * angle_offset);
            const float begin_a = a - PI_DIV_2;
            float roff = ImMax(ImSin(start) - 0.5f, 0.f) * (radius * 0.4f);
            switch (mode) {
            case 1: roff = ease_inoutquad(roff); break;
            case 2: roff = ease_inoutexpo(roff) * (radius * 0.3f); break;
            }
            ImVec2 tri_centre(centre.x + ImCos(a) * (radius1 + roff), centre.y + ImSin(a) * (radius1 + roff));
            for (int pi = 0; pi < 4; ++pi) {
                points[pi] = {tri_centre.x + ImCos(begin_a+ pi * PI_DIV_2) * radius1, tri_centre.y + ImSin(begin_a + pi * PI_DIV_2) * radius1};
            }
            window->DrawList->AddConvexPolyFilled(points.data(), 4, color_alpha(color, 1.f));
        }
    }

    inline void SpinnerMoonLine(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = red, float speed = 2.8f, float angle = IM_PI)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float angle_offset = (angle * 0.5f) / num_segments;
      const float th = thickness / num_segments;

      window->DrawList->AddCircleFilled(centre, radius, bg, num_segments);

      auto draw_gradient = [&] (const std::function<float (int)>& b, const std::function<float (int)>& e, const std::function<float (int)>& th) {
        for (int i = 0; i < num_segments; i++)
        {
          window->DrawList->AddLine(ImVec2(centre.x + ImCos(start + b(i)) * radius, centre.y + ImSin(start + b(i)) * radius),
                                    ImVec2(centre.x + ImCos(start + e(i)) * radius, centre.y + ImSin(start + e(i)) * radius),
                                    color_alpha(color, 1.f),
                                    th(i));
        }

      };

      draw_gradient([&] (int i) { return (num_segments + i) * angle_offset; },
                    [&] (int i) { return (num_segments + i + 1) * angle_offset; },
                    [&] (int i) { return thickness - th * i; });

      draw_gradient([&] (int i) { return (i) * angle_offset; },
                    [&] (int i) { return (i + 1) * angle_offset; },
                    [&] (int i) { return th * i; });

      draw_gradient([&] (int i) { return (num_segments + i) * angle_offset; },
                    [&] (int i) { return (num_segments + i + 1) * angle_offset; },
                    [&] (int i) { return thickness - th * i; });

      const float b_angle_offset = (PI_2 - angle) / num_segments; 
      draw_gradient([&] (int i) { return num_segments * angle_offset * 2.f + (i * b_angle_offset); },
                    [&] (int i) { return num_segments * angle_offset * 2.f + ((i + 1) * b_angle_offset); },
                    [] (int) { return 1.f; });
    }

    inline void SpinnerCircleDrop(const char *label, float radius, float thickness, float thickness_drop, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, float angle = IM_PI)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime() * speed;
      const float bg_angle_offset = PI_2 / num_segments;
      const float angle_offset = angle / num_segments;
      const float th = thickness_drop / num_segments;
      const float drop_radius_th = thickness_drop / num_segments;

      for (int i = 0; i < num_segments; i++)
      {
        const float a = start + (i * angle_offset);
        const float a1 = start + ((i + 1) * angle_offset);
        const float s_drop_radius = radius - thickness / 2.f - (drop_radius_th * i);
        window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * s_drop_radius, centre.y + ImSin(a) * s_drop_radius),
                                  ImVec2(centre.x + ImCos(a1) * s_drop_radius, centre.y + ImSin(a1) * s_drop_radius),
                                  color_alpha(color, 1.f),
                                  th * 2.f * i);
      }
      const float ai_end = start + (num_segments * angle_offset);
      const float f_drop_radius = radius - thickness / 2.f - thickness_drop;
      ImVec2 circle_i_center{centre.x + ImCos(ai_end) * f_drop_radius, centre.y + ImSin(ai_end) * f_drop_radius};
      window->DrawList->AddCircleFilled(circle_i_center, thickness_drop, color_alpha(color, 1.f), num_segments);

      window->DrawList->PathClear();
      for (int i = 0; i <= num_segments; i++)
      {
        const float a = (i * bg_angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
      }
      detail::PathStroke(window->DrawList, color_alpha(bg, 1.f), thickness, false);
    }

    inline void SpinnerSurroundedIndicator(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
      window->DrawList->AddCircleFilled(centre, thickness, color_alpha(bg, 1.f), num_segments);
      window->DrawList->AddCircleFilled(centre, thickness, color_alpha(color, ImMax(0.1f, ImMin(lerp_koeff, 1.f))), num_segments);

      auto PathArc = [&] (const ImColor& c, float th) {
        window->DrawList->PathClear();
        const float bg_angle_offset = PI_2 / num_segments;
        for (int i = 0; i <= num_segments; i++)
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(i * bg_angle_offset) * radius, centre.y + ImSin(i * bg_angle_offset) * radius));
        detail::PathStroke(window->DrawList, color_alpha(c, 1.f), th, false);
      };
      
      lerp_koeff = (ImSin((float)ImGui::GetTime() * speed * 1.6f) + 1.f) * 0.5f;
      PathArc(bg, thickness);
      PathArc(color_alpha(color, 1.f - ImMax(0.1f, ImMin(lerp_koeff, 1.f))), thickness);
    }

    inline void SpinnerWifiIndicator(const char *label, float radius, float thickness, const ImColor &color = red, const ImColor &bg = half_white, float speed = 2.8f, float cangle = 0.f, int dots = 3)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
        float start_ang = -cangle - PI_DIV_4 - PI_DIV_2;
        ImVec2 pc(centre.x + ImSin(cangle) * radius, centre.y + ImCos(cangle) * radius);
        window->DrawList->AddCircleFilled(pc, thickness, bg, num_segments);
        window->DrawList->AddCircleFilled(pc, thickness, color_alpha(color, ImMax(0.1f, ImMin(lerp_koeff, 1.f))), num_segments);

        auto PathArc = [&] (float as, const ImColor& c, float th, float r) {
            window->DrawList->PathClear();
            const float bg_angle_offset = PI_DIV(2) / num_segments;
            for (int i = 0; i <= num_segments; i++)
                window->DrawList->PathLineTo(ImVec2(pc.x + ImCos(as + i * bg_angle_offset) * r, pc.y + ImSin(as + i * bg_angle_offset) * r));
            detail::PathStroke(window->DrawList, color_alpha(c, 1.f), th, false);
        };

        const float interval = (size.x * 0.7f) / dots;
        for (int i = 0; i < dots; ++i) {
            float r = 1.5f * (i + 1) * interval;
            lerp_koeff = (ImSin((float)ImGui::GetTime() * speed - (i+1) * (IM_PI / dots)) + 1.f) * 0.5f;
            PathArc(start_ang, bg, thickness, r);
            PathArc(start_ang, color_alpha(color, ImMax(0.1f, ImMin(lerp_koeff, 1.f))), thickness, r);
        }
    }

    inline void SpinnerTrianglesSelector(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, size_t bars = 8)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
      ImColor c = color_alpha(color, ImMax(0.1f, ImMin(lerp_koeff, 1.f)));
      float dr = radius - thickness - 3;
      window->DrawList->AddCircleFilled(centre, dr, bg, num_segments);
      window->DrawList->AddCircleFilled(centre, dr, c, num_segments);

      // Render
      float start = (float)ImGui::GetTime() * speed;
      float astart = ImFmod(start, PI_2 / bars);
      start -= astart;
      const float angle_offset = PI_2 / bars;
      const float angle_offset_t = angle_offset * 0.3f;
      bars = ImMin<size_t>(bars, 32);

      const float rmin = radius - thickness;
      auto get_points = [&] (float left, float right) -> std::array<ImVec2, 4> {
        return {
          ImVec2(centre.x + ImCos(left) * rmin, centre.y + ImSin(left) * rmin),
          ImVec2(centre.x + ImCos(left) * radius, centre.y + ImSin(left) * radius),
          ImVec2(centre.x + ImCos(right) * radius, centre.y + ImSin(right) * radius),
          ImVec2(centre.x + ImCos(right) * rmin, centre.y + ImSin(right) * rmin)
        };
      };

      auto draw_sectors = [&] (float s, const std::function<ImU32 (size_t)>& color_func) {
        for (size_t i = 0; i <= bars; i++) {
          float left = s + (i * angle_offset) - angle_offset_t;
          float right = s + (i * angle_offset) + angle_offset_t;
          auto points = get_points(left, right);
          window->DrawList->AddConvexPolyFilled(points.data(), 4, color_func(i));
        }
      };

      draw_sectors(0, [&] (size_t) { return color_alpha(bg, 0.1f); });
      draw_sectors(start, [&] (size_t i) { return color_alpha(bg, (i / (float)bars) - 0.5f); });
    }

    using LeafColor = ImColor (int);
    inline void SpinnerCamera(const char *label, float radius, float thickness, LeafColor *leaf_color, float speed = 2.8f, size_t bars = 8, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float start = (float)ImGui::GetTime() * speed;
      switch (mode) {
      case 1:
        start += ease_inoutquad(ImSin(ImFmod(start, IM_PI)));
        thickness += ease_inoutquad(ImSin(ImFmod(start, IM_PI))) * (thickness * 0.5f);
        break;
      }
      const float angle_offset = PI_2 / bars;
      const float angle_offset_t = angle_offset * 0.3f;
      bars = ImMin<size_t>(bars, 32);

      const float rmin = radius - thickness - 1;
      auto get_points = [&] (float left, float right) -> std::array<ImVec2, 4> {
        return {
          ImVec2(centre.x + ImCos(left - 0.1f) * radius, centre.y + ImSin(left - 0.1f) * radius),
          ImVec2(centre.x + ImCos(right + 0.15f) * radius, centre.y + ImSin(right + 0.15f) * radius),
          ImVec2(centre.x + ImCos(right - 0.91f) * rmin, centre.y + ImSin(right - 0.91f) * rmin)
        };
      };

      auto draw_sectors = [&] (float s, const std::function<ImU32 (int)>& color_func) {
        for (size_t i = 0; i <= bars; i++) {
          float left = s + (i * angle_offset) - angle_offset_t;
          float right = s + (i * angle_offset) + angle_offset_t;
          auto points = get_points(left, right);
          window->DrawList->AddConvexPolyFilled(points.data(), 3, color_alpha(color_func((int)i), 1.f));
        }
      };

      draw_sectors(start, leaf_color);
    }

    inline void SpinnerFlowingGradient(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = red, float speed = 2.8f, float angle = IM_PI)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float angle_offset = (angle * 0.5f) / num_segments;
      const float bg_angle_offset = (PI_2) / num_segments;
      const float th = thickness / num_segments;

      for (size_t i = 0; i <= num_segments; i++)
      {
        const float a = (i * bg_angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
      }
      detail::PathStroke(window->DrawList, bg, thickness, false);

      auto draw_gradient = [&] (const std::function<float (size_t)>& b, const std::function<float (size_t)>& e, const std::function<ImU32 (size_t)>& c) {
        for (size_t i = 0; i < num_segments; i++)
        {
          window->DrawList->AddLine(ImVec2(centre.x + ImCos(start + b(i)) * radius, centre.y + ImSin(start + b(i)) * radius),
                                    ImVec2(centre.x + ImCos(start + e(i)) * radius, centre.y + ImSin(start + e(i)) * radius),
                                    c(i),
                                    thickness);
        }
      };

      draw_gradient([&] (size_t i) { return (i) * angle_offset; },
                    [&] (size_t i) { return (i + 1) * angle_offset; },
                    [&] (size_t i) { return color_alpha(color, (i / (float)num_segments)); });

      draw_gradient([&] (size_t i) { return (num_segments + i) * angle_offset; },
                    [&] (size_t i) { return (num_segments + i + 1) * angle_offset; },
                    [&] (size_t i) { return color_alpha(color, 1.f - (i / (float)num_segments)); });
    }

    inline void SpinnerRotateSegments(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, size_t layers = 1, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float start = (float)ImGui::GetTime()* speed;
      switch (mode) {
      case 1: start += ease_inoutquad(ImSin(ImFmod(start, IM_PI))); break;
      }
      const float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      float r = radius;
      float reverse = 1.f;
      for (size_t layer = 0; layer < layers; layer++)
      {
        for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
        {
          window->DrawList->PathClear();
          for (size_t i = 2; i <= num_segments - 2; i++)
          {
            const float a = start * (1 + 0.1f * layer) + arc_angle * arc_num + (i * angle_offset);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a * reverse) * r, centre.y + ImSin(a * reverse) * r));
          }
          detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
        }

        r -= (thickness + 1);
        reverse *= -1.f;
      }
    }

    inline void SpinnerLemniscate(const char* label, float radius, float thickness, const ImColor& color = white, float speed = 2.8f, float angle = IM_PI / 2.0f)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime() * speed;
      const float a = radius;
      const float t = start;
      const float step = angle / num_segments;
      const float th = thickness / num_segments;

      for (size_t i = 0; i < num_segments; i++)
      {
          const auto xy0 = damped_infinity(start + (i * step), a);
          const auto xy1 = damped_infinity(start + ((i + 1) * step), a);
      
          window->DrawList->AddLine(ImVec2(centre.x + xy0.first, centre.y + xy0.second),
              ImVec2(centre.x + xy1.first, centre.y + xy1.second),
              color_alpha(color, 1.f),
              th * i);
      }
    }

    inline void SpinnerRotateGear(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t pins = 12)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      const float bg_angle_offset = PI_2 / num_segments;
      const float bg_radius = radius - thickness;

      window->DrawList->PathClear();
      for (size_t i = 0; i <= num_segments; i++)
      {
        const float a = (i * bg_angle_offset);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * bg_radius, centre.y + ImSin(a) * bg_radius));
      }
      detail::PathStroke(window->DrawList, color_alpha(color, 1.f), bg_radius / 2, false);

      const float rmin = bg_radius;
      const float rmax = radius;
      const float pin_angle_offset = PI_2 / pins;
      for (size_t i = 0; i <= pins; i++)
      {
        float a = start + (i * pin_angle_offset);
        window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * rmin, centre.y + ImSin(a) * rmin),
                                  ImVec2(centre.x + ImCos(a) * rmax, centre.y + ImSin(a) * rmax),
                                  color_alpha(color, 1.f), thickness);
      }
    }

    inline void SpinnerRotateWheel(const char *label, float radius, float thickness, const ImColor &bg_color = white, const ImColor &color = white, float speed = 2.8f, size_t pins = 12)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime() * speed;
        const float bg_radius = radius - thickness;
        const float line_th = ImMax(radius / 8.f, 3.f);

        auto draw_circle = [window, num_segments, centre] (float r, const ImColor &c, float th) {
            const float bg_angle_offset = PI_2 / num_segments;
            window->DrawList->PathClear();
            for (size_t i = 0; i <= num_segments; i++)
            {
                const float a = (i * bg_angle_offset);
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * r, centre.y + ImSin(a) * r));
            }
            detail::PathStroke(window->DrawList, color_alpha(c, 1.f), th, false);
        };

        auto draw_pins = [window, centre, pins, start] (float rmin, float rmax, const ImColor &c, float th) {
            const float pin_angle_offset = PI_2 / pins;
            for (size_t i = 0; i <= pins; i++)             {
                float a = start + (i * pin_angle_offset);
                window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * rmin, centre.y + ImSin(a) * rmin),
                                          ImVec2(centre.x + ImCos(a) * rmax, centre.y + ImSin(a) * rmax),
                                          color_alpha(c, 1.f), th);
            }
        };

        draw_circle(bg_radius, bg_color, line_th);
        draw_pins(bg_radius, radius, bg_color, line_th);
        draw_circle(radius, color, line_th);
    }

    inline void SpinnerAtom(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      elipses = std::min<int>(elipses, 3);

      auto draw_rotated_ellipse = [&] (float alpha, float start) {
        alpha = ImFmod(alpha, IM_PI);
        float a = radius;
        float b = radius / 2.f; 

        window->DrawList->PathClear();
        for (int i = 0; i < num_segments; ++i) {
          float anga = (i * (PI_2 / (num_segments - 1)));

          float xx = a * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + centre.x;
          float yy = b * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + centre.y;
          window->DrawList->PathLineTo({xx, yy});
        }
        detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);

        float anga = ImFmod(start, PI_2);
        float x = a * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + centre.x;
        float y = b * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + centre.y;
        return ImVec2{x, y};
      };

      ImVec2 ppos[3];
      for (int i = 0; i < elipses; ++i) {
        ppos[i % 3] = draw_rotated_ellipse((IM_PI * (float)i/ elipses), start * (1.f + 0.1f * i));
      }

      ImColor pcolors[3] = {ImColor(255, 0, 0), ImColor(0, 255, 0), ImColor(0, 0, 255)};
      for (int i = 0; i < elipses; ++i) {
        window->DrawList->AddCircleFilled(ppos[i], thickness * 2, color_alpha(pcolors[i], 1.f), int(num_segments / 3.f));
      }
    }

    inline void SpinnerPatternRings(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime()* speed;
        elipses = std::max<int>(elipses, 1);

        auto draw_rotated_ellipse = [&] (float alpha, float tr, float y) {
            alpha = ImFmod(alpha, IM_PI);
            float a = radius;
            float b = radius / 2.f; 

            const float bg_angle_offset = PI_2 / (num_segments - 1);
            ImColor c = color_alpha(color, tr);
            window->DrawList->PathClear();
            for (int i = 0; i < num_segments; ++i) {
                float anga = (i * bg_angle_offset);
                float xx = a * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + centre.x;
                float yy = b * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + centre.y + y;
                window->DrawList->PathLineTo({xx, yy});
            }
            detail::PathStroke(window->DrawList, c, thickness, false);
        };

        for (int i = 0; i < elipses; ++i)
        {
            const float h = (0.5f * ImSin(start + (IM_PI / elipses) * i));
            draw_rotated_ellipse(0.f, 0.1f + (0.9f / elipses) * i, radius * h);
        }
    }

    inline void SpinnerPatternEclipse(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3, float delta_a = 2.f, float delta_y = 0.f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime()* speed;
        elipses = std::max<int>(elipses, 1);

        auto draw_rotated_ellipse = [&] (const ImVec2 &pp, float alpha, float tr, float r, float x, float y) {
            alpha = ImFmod(alpha, IM_PI);
            float a = r;
            float b = r / delta_a; 

            ImColor c = color_alpha(color, tr);
            window->DrawList->PathClear();
            for (int i = 0; i < num_segments; ++i) {
                float anga = (i *  PI_2 / (num_segments - 1));
                float xx = a * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + pp.x + x;
                float yy = b * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + pp.y + y;
                window->DrawList->PathLineTo({xx, yy});
            }
            detail::PathStroke(window->DrawList, c, thickness, false);
        };

        for (int i = 0; i < elipses; ++i)
        {
            const float rkoeff = (0.5f + 0.5f * ImSin(start + (IM_PI / elipses) * i));
            const float h = ((1.f / elipses) * (i+1));
            const float anga = start + (i *  PI_DIV_2 / elipses);
            const float yoff = ((1.f / elipses) * i) * delta_y;
            float a = (radius * (1.f - h));
            float b = (radius * (1.f - h)) / delta_a; 
            float xx = a * ImCos(anga) * ImCos(0) + b * ImSin(anga) * ImSin(0) + centre.x;
            float yy = b * ImSin(anga) * ImCos(0) - a * ImCos(anga) * ImSin(0) + centre.y;
            draw_rotated_ellipse(ImVec2(xx, yy), 0.f, 0.3f + (0.7f / elipses) * i, radius * h, 0.f, yoff * radius);
        }
    }

    inline void SpinnerPatternSphere(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed * 3.f, size.y);
        elipses = std::max<int>(elipses, 1);

        auto draw_rotated_ellipse = [&] (float alpha, float tr, float y, float r) {
            alpha = ImFmod(alpha, IM_PI);
            float a = r;
            float b = r / 4.f; 

            ImColor c = color_alpha(color, tr);
            window->DrawList->PathClear();
            for (int i = 0; i < num_segments; ++i) {
                float anga = (i * PI_2 / (num_segments - 1));
                float xx = a * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + centre.x;
                float yy = b * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + pos.y + y;
                window->DrawList->PathLineTo({xx, yy});
            }
            detail::PathStroke(window->DrawList, c, thickness, false);
        };

        float offset = 0;
        float half_size = size.y * 0.5f;
        for (size_t i = 0; i < elipses; i++)
        {
            offset = ImFmod(start + i * (size.y / elipses), size.y);
            const float th = (offset > half_size)
                                ? 1.f - (offset - half_size) / half_size
                                : offset / half_size;
            draw_rotated_ellipse(0.f, th, offset, ImSin(offset / size.y * IM_PI) * radius);
        }
    }

    inline void SpinnerRingSynchronous(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
       
        num_segments *= 4;
        const float aoffset = ImFmod((float)ImGui::GetTime(), PI_2);
        const float bofsset = (aoffset > IM_PI) ? IM_PI : aoffset;
        const float angle_offset = PI_2 / num_segments;
        float ared_min = 0, ared = 0;
        if (aoffset > IM_PI)
            ared_min = aoffset - IM_PI;

        auto draw_ellipse = [&] (float alpha, float y, float r) {
            window->DrawList->PathClear();
            for (size_t i = 0; i <= num_segments + 1; i++)
            {
                ared = start + (i * angle_offset);

                if (i * angle_offset < ared_min * 2)
                    continue;

                if (i * angle_offset > bofsset * 2.f)
                    break;

                float a = r;
                float b = r * 0.25f;

                const float xx = a * ImCos(ared) * ImCos(alpha) + b * ImSin(ared) * ImSin(alpha) + centre.x;
                const float yy = b * ImSin(ared) * ImCos(alpha) - a * ImCos(ared) * ImSin(alpha) + pos.y + y;
                window->DrawList->PathLineTo(ImVec2(xx, yy));
            }
            detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
        };

        for (int i = 0; i < elipses; ++i)
        {
            float y = i * ((float)(size.y * 0.7f) / (float)elipses) + (size.y * 0.15f);
            draw_ellipse(0, y, radius * ImSin((i + 1) * (IM_PI / (elipses + 1))));
        }
    }

    inline void SpinnerRingWatermarks(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = (float)ImGui::GetTime() * speed;
        num_segments *= 4;

        const float angle_offset = PI_2 / num_segments;
        auto draw_ellipse = [&] (float s, float alpha, float x, float y, float r) {
            const float aoffset = ImFmod((float)s, PI_2);
            const float bofsset = (aoffset > IM_PI) ? IM_PI : aoffset;
            float ared_min = 0, ared = 0;
            if (aoffset > IM_PI)
                ared_min = aoffset - IM_PI;

            window->DrawList->PathClear();
            for (size_t i = 0; i <= num_segments + 1; i++)
            {
                ared = s + (i * angle_offset);

                if (i * angle_offset < ared_min * 2)
                    continue;

                if (i * angle_offset > bofsset * 2.f)
                    break;

                float a = r;
                float b = r * 0.25f;

                const float xx = a * ImCos(ared) * ImCos(alpha) + b * ImSin(ared) * ImSin(alpha) + centre.x + x;
                const float yy = b * ImSin(ared) * ImCos(alpha) - a * ImCos(ared) * ImSin(alpha) + pos.y + y;
                window->DrawList->PathLineTo(ImVec2(xx, yy));
            }
            detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
        };

        for (int i = 0; i < elipses; ++i)
        {
            float y = i * ((float)(size.y * 0.7f) / (float)elipses) + (size.y * 0.15f);
            float x = -i * (radius / elipses);
            draw_ellipse(start + (i * IM_PI / (elipses * 2)), -PI_DIV_4, x, y, radius);
        }
    }

    inline void SpinnerRotatedAtom(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, int elipses = 3, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = (float)ImGui::GetTime()* speed;
      auto draw_rotated_ellipse = [&] (float alpha) {
        std::array<ImVec2, 36> pts;

        alpha = ImFmod(alpha, IM_PI);
        float a = radius;
        float b = radius / 2.f; 

        const float bg_angle_offset = PI_2 / num_segments;
        for (size_t i = 0; i < num_segments; ++i) {
          float anga = (i * bg_angle_offset);
          float h = ease((ease_mode)mode, start, radius);

          pts[i].x = (a + h) * ImCos(anga) * ImCos(alpha) + b * ImSin(anga) * ImSin(alpha) + centre.x;
          pts[i].y = (b + h) * ImSin(anga) * ImCos(alpha) - a * ImCos(anga) * ImSin(alpha) + centre.y;
        }
        for (size_t i = 1; i < num_segments; ++i) {
          window->DrawList->AddLine(pts[i-1], pts[i], color_alpha(color, 1.f), thickness);
        }
        window->DrawList->AddLine(pts[num_segments-1], pts[0], color_alpha(color, 1.f), thickness);
      };

      for (int i = 0; i < elipses; ++i) {
        draw_rotated_ellipse(start + (IM_PI * (float)i/ elipses));
      }
    }

    inline void SpinnerRainbowBalls(const char *label, float radius, float thickness, const ImColor &color, float speed, int balls = 5, int mode = 0, int rings = 1, int mx = 1)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float start = ImFmod((float)ImGui::GetTime() * speed * 3.f, IM_PI);
      const float colorback = 0.3f + 0.2f * ImSin((float)ImGui::GetTime() * speed);
      const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
      const float radius1 = (0.7f + 0.2f * ImSin(start)) * radius;
      const float angle_offset = PI_2 / balls;
      const bool rainbow = ((ImU32)color.Value.w) == 0;

      float out_h, out_s, out_v;
      ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
      for (int j = 0; j < rings; j++) {
          const float ring_r = radius1 * (rings > 1 ? ((j+1) / (float)rings) : 1);
          float ring_angle = (j * (PI_2 / rings));
          for (int i = 0; i < balls; i++) {
            const float a = rstart + (i * angle_offset) + ring_angle;
            ImColor c = rainbow ? ImColor::HSV(out_h + i * (1.f / balls) + colorback, out_s, out_v) : color;
            float rb = ring_r + ease((ease_mode)mode, start, ring_r);
            float ax, ay;
            switch (mx) {
            default: ax = ImCos(a) * rb; ay = ImSin(a) * rb; break;
            case 1: ax = ImCos(a) * rb; ay = ImSin(a) * rb + ImSin(a * 8) * rb * 0.15f; break;
            case 2: { float pulse = 0.8f + 0.2f * ImSin(start * 2); ax = ImCos(a) * rb * pulse; ay = ImSin(a) * rb * pulse; } break;
            case 3: { float R = rb, r = rb * 0.3f, d = rb * 0.1f; float t = a; ax = (R - r) * ImCos(t) + d * ImCos((R - r) / r * t); ay = (R - r) * ImSin(t) - d * ImSin((R - r) / r * t); } break;
            case 4: { float t = a; ax = rb * ImCos(t) / (1 + ImSin(t) * ImSin(t)); ay = rb * ImSin(t) * ImCos(t) / (1 + ImSin(t) * ImSin(t)); } break;
            case 5: { float zigzag = (ImSin(a * 8) > 0) ? rb * 0.3f : -rb * 0.3f; ax = ImCos(a) * rb + zigzag * ImCos(a + IM_PI / 2); ay = ImSin(a) * rb + zigzag * ImSin(a + IM_PI / 2); } break;
            case 6: ax = ImCos(a) * (rb + ImSin(a * 3) * rb * 0.3f); ay = ImSin(a) * (rb + ImSin(a * 3) * rb * 0.3f); break;
            case 7:
                {
                    float t = ImFmod(a, PI_2) / PI_2;
                    if (t < 0.25f) { ax = rb; ay = (t * 4 - 0.5f) * 2 * rb; } 
                    else if (t < 0.5f) { ax = (0.75f - t * 4) * 2 * rb; ay = rb; } 
                    else if (t < 0.75f) { ax = -rb; ay = (1.25f - t * 4) * 2 * rb; } 
                    else { ax = (t * 4 - 1.75f) * 2 * rb; ay = -rb; }
                }
                break;
            case 8: ax = ImCos(a) * rb; ay = ImSin(a) * rb + ImSin(a * 4) * rb * 0.2f; break;
            }
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ax, centre.y + ay), thickness, color_alpha(c, 1.f), num_segments);
          }
      }
    }

    inline void SpinnerRainbowShot(const char *label, float radius, float thickness, const ImColor &color, float speed, int balls = 5, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed * 3.f, PI_2);
        const float colorback = 0.3f + 0.2f * ImSin((float)ImGui::GetTime() * speed);
        const float rstart = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float angle_offset = PI_2 / balls;
        const bool rainbow = ((ImU32)color.Value.w) == 0;

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
        for (int i = 0; i <= balls; i++)
        {
            float centera = start - PI_DIV_2 + (i * angle_offset);
            float rmul = ImMax(0.2f, 1.f - ImSin(centera));
            const float radius1 = ImMin(radius * rmul, radius);
            const float a = (i * angle_offset);
            ImColor c = rainbow ? ImColor::HSV(out_h + i * (1.f / balls) + colorback, out_s, out_v) : color;
            float ax, ay;
            switch(mode) {
            default: ax = ImCos(a) * radius1; ay = ImSin(a) * radius1; break;
            case 1: ax = ImCos(a) * radius1; ay = ImSin(a) * radius1 + ImSin(a * 4) * radius1 * 0.2f; break;
            case 2: { float pulse = 0.8f + 0.2f * ImSin(start * 2); ax = ImCos(a) * radius1 * pulse; ay = ImSin(a) * radius1 * pulse; } break;
            case 3: { float R = radius1, r = radius1 * 0.3f, d = radius1 * 0.1f; float t = a; ax = (R - r) * ImCos(t) + d * ImCos((R - r) / r * t); ay = (R - r) * ImSin(t) - d * ImSin((R - r) / r * t); } break;
            case 4: { float t = a; ax = radius1 * ImCos(t) / (1 + ImSin(t) * ImSin(t)); ay = radius1 * ImSin(t) * ImCos(t) / (1 + ImSin(t) * ImSin(t)); } break;
            case 5: { float zigzag = (ImSin(a * 8) > 0) ? radius1 * 0.3f : -radius1 * 0.3f; ax = ImCos(a) * radius1 + zigzag * ImCos(a + IM_PI / 2); ay = ImSin(a) * radius1 + zigzag * ImSin(a + IM_PI / 2); } break;
            case 6: ax = ImCos(a) * (radius1 + ImSin(a * 3) * radius1 * 0.3f); ay = ImSin(a) * (radius1 + ImSin(a * 3) * radius1 * 0.3f); break;
            }
            window->DrawList->AddLine(centre, ImVec2(centre.x + ax, centre.y + ay), color_alpha(c, 1.f), thickness);
        }
    }

    inline void SpinnerSpiral(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        float a = radius / num_segments;
        float b = a;

        ImVec2 last = centre;
        for (size_t arc_num = 0; arc_num < (num_segments * arcs); ++arc_num)
        {
            float angle = (PI_2 / num_segments) * arc_num;
            float x = centre.x + (a + b * angle) * ImCos(start + angle);
            float y = centre.y + (a + b * angle) * ImSin(start + angle);

            window->DrawList->AddLine(last, ImVec2(x, y), color_alpha(color, 1.f), thickness);
            last = ImVec2(x, y);
        }
    }

    inline void SpinnerSpiralEye(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        float a = (radius * 3.f) / num_segments;
        float b = a;
        num_segments *= 4;

        auto half_eye = [&] (float side) {
            for (size_t arc_num = 0; arc_num < num_segments; ++arc_num)         {
                float angle = (PI_2 / num_segments) * arc_num;
                float x = centre.x + (a + b * angle) * ImCos((start + angle) * side);
                float y = centre.y + (a + b * angle) * ImSin((start + angle) * side);

                window->DrawList->AddCircleFilled(ImVec2(x, y), thickness, color_alpha(color, 1.f));
            }
        };

        half_eye(1.f);
        half_eye(-1.f);
    }


    inline void SpinnerBlocks(const char *label, float radius, float thickness, const ImColor &bg, const ImColor &color, float speed)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      ImVec2 lt{centre.x - radius, centre.y - radius};
      const float offset_block = radius * 2.f / 3.f;

      int start = (int)ImFmod((float)ImGui::GetTime() * speed, 8.f);

      const ImVec2ih poses[] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};

      int ti = 0;
      for (const auto &rpos: poses)
      {
          const ImColor &c = (ti == start) ? color : bg;
          window->DrawList->AddRectFilled(ImVec2(lt.x + rpos.x * (offset_block), lt.y + rpos.y * offset_block),
                                          ImVec2(lt.x + rpos.x * (offset_block) + thickness, lt.y + rpos.y * offset_block + thickness),
                                          color_alpha(c, 1.f));
          ti++;
      }
    }

    inline void SpinnerTwinBlocks(const char *label, float radius, float thickness, const ImColor &bg, const ImColor &color, float speed)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float offset_block = radius * 2.f / 3.f;
        ImVec2 lt{centre.x - radius - offset_block / 2.f, centre.y - radius - offset_block / 2.f};

        int start = (int)ImFmod((float)ImGui::GetTime() * speed, 8.f);
        const ImVec2ih poses[] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};

        int ti = 0;
        for (const auto &rpos: poses)
        {
            const ImColor &c = (ti == start) ? color : bg;
            window->DrawList->AddRectFilled(ImVec2(lt.x + rpos.x * (offset_block), lt.y + rpos.y * offset_block),
                                            ImVec2(lt.x + rpos.x * (offset_block) + thickness, lt.y + rpos.y * offset_block + thickness),
                                            color_alpha(c, 1.f));
            ti++;
        }

        lt = ImVec2{centre.x - radius + offset_block / 2.f, centre.y - radius + offset_block / 2.f};
        ti = std::size(poses) - 1;
        start = (int)ImFmod((float)ImGui::GetTime() * speed * 1.1f, 8.f);
        for (const auto &rpos: poses)
        {
            const ImColor &c = (ti == start) ? color : bg;
            window->DrawList->AddRectFilled(ImVec2(lt.x + rpos.x * (offset_block), lt.y + rpos.y * offset_block),
                                            ImVec2(lt.x + rpos.x * (offset_block) + thickness, lt.y + rpos.y * offset_block + thickness),
                                            color_alpha(c, 1.f));
            ti--;
        }
    }


    inline void SpinnerScaleBlocks(const char *label, float radius, float thickness, const ImColor &color, float speed, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      ImVec2 lt{centre.x - radius, centre.y - radius};
      const float offset_block = radius * 2.f / 3.f;

      const ImVec2ih poses[] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
      constexpr float rkoeff[9] = {0.1f, 0.15f, 0.17f, 0.25f, 0.6f, 0.15f, 0.1f, 0.12f, 0.22f};

      int ti = 0;
      float out_h, out_s, out_v;
      ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
      for (const auto &rpos: poses)
      {
        ImColor c = ImColor::HSV(out_h + ti * 0.1f, out_s, out_v);
        if (mode) {
            float h = (0.1f + 0.4f * ImSin((float)ImGui::GetTime() * (speed * rkoeff[ti % 9])));
            window->DrawList->AddCircleFilled(ImVec2(lt.x + rpos.x * (offset_block), lt.y + rpos.y * offset_block), std::max<float>(1.f, h * thickness),
                                              color_alpha(c, 1.f));
        } else {
            float h = (0.8f + 0.4f * ImSin((float)ImGui::GetTime() * (speed * rkoeff[ti % 9])));
            window->DrawList->AddRectFilled(ImVec2(lt.x + rpos.x * (offset_block), lt.y + rpos.y * offset_block),
                                           ImVec2(lt.x + rpos.x * (offset_block) + h * thickness, lt.y + rpos.y * offset_block + h * thickness),
                                           color_alpha(c, 1.f));
        }
        ti++;
      }
    }

    inline void SpinnerScaleSquares(const char *label, float radius, float thikness, const ImColor &color, float speed)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        ImVec2 lt{centre.x - radius, centre.y - radius};
        const float offset_block = radius * 2.f / 3.f;
        const float hside = (thikness / 2.f);

        const ImVec2ih poses[] = {{0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2}, {2, 1}, {1, 2}, {2, 2}};
        const float offsets[] =  {0.f,    0.8f,   0.8f,   1.6f,   1.6f,   1.6f,   2.4f,   2.4f,   3.2f};

        int ti = 0;
        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
        for (const auto &rpos: poses)
        {
            const ImColor c = ImColor::HSV(out_h + offsets[ti], out_s, out_v);
            const float strict = (0.5f + 0.5f * ImSin((float)-ImGui::GetTime() * speed + offsets[ti % 9]));
            const float side = ImClamp<float>(strict + 0.1f, 0.1f, 1.f) * hside;
            window->DrawList->AddRectFilled(ImVec2(lt.x + hside + (rpos.x * offset_block) - side, lt.y + hside + (rpos.y * offset_block) - side),
                                            ImVec2(lt.x + hside + (rpos.x * offset_block) + side, lt.y + hside + (rpos.y * offset_block) + side),
                                            color_alpha(c, 1.f));
            ti++;
        }
    }

    inline void SpinnerSquishSquare(const char *label, float radius, const ImColor &color, float speed)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);
        
        float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float side = ImSin((float)-start) * radius;
        bool type = (start > IM_PI) ? 1 : 0;
        if (type) {
            if (start > IM_PI && start < IM_PI + PI_DIV_2) {
                window->DrawList->AddRectFilled(ImVec2(centre.x - side, centre.y - radius), ImVec2(centre.x + side, centre.y + radius), color_alpha(color, 1.f));
            } else {
                window->DrawList->AddRectFilled(ImVec2(centre.x - radius, centre.y - side), ImVec2(centre.x + radius, centre.y + side), color_alpha(color, 1.f));
            }
        } else {
            if (start < PI_DIV_2) {
                window->DrawList->AddRectFilled(ImVec2(centre.x - radius, centre.y - side), ImVec2(centre.x + radius, centre.y + side), color_alpha(color, 1.f));
            } else {
                window->DrawList->AddRectFilled(ImVec2(centre.x - side, centre.y - radius), ImVec2(centre.x + side, centre.y + radius), color_alpha(color, 1.f));
            }
        }
    }


    inline void SpinnerArcPolarFade(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      constexpr float rkoeff[6][3] = {{0.15f, 0.1f, 0.1f}, {0.033f, 0.15f, 0.8f}, {0.017f, 0.25f, 0.6f}, {0.037f, 0.1f, 0.4f}, {0.25f, 0.1f, 0.3f}, {0.11f, 0.1f, 0.2f}};
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
      {
        const float b = arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
        const float e = arc_angle * arc_num + arc_angle - PI_DIV_2 - PI_DIV_4;
        const float a = arc_angle * arc_num;
        float h = (0.6f + 0.3f * ImSin((float)ImGui::GetTime() * (speed * rkoeff[arc_num % 6][2] * 2.f) + (2 * rkoeff[arc_num % 6][0])));
        ImColor c = color_alpha(color, h);
        c.Value.w += ease((ease_mode)mode, h, arc_angle);

        window->DrawList->PathClear();
        window->DrawList->PathLineTo(centre);
        for (size_t i = 0; i <= num_segments + 1; i++)
        {
          const float ar = arc_angle * arc_num + (i * angle_offset) - PI_DIV_2 - PI_DIV_4;
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar) * radius, centre.y + ImSin(ar) * radius));
        }
        window->DrawList->PathFillConvex(c);
      }
    }

    inline void SpinnerArcPolarRadius(const char *label, float radius, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float arc_angle = PI_2 / (float)arcs;
      const float angle_offset = arc_angle / num_segments;
      constexpr float rkoeff[6][3] = {{0.15f, 0.1f, 0.41f}, {0.033f, 0.15f, 0.8f}, {0.017f, 0.25f, 0.6f}, {0.037f, 0.1f, 0.4f}, {0.25f, 0.1f, 0.3f}, {0.11f, 0.1f, 0.2f}};
      float out_h, out_s, out_v;
      const float start = (float)ImGui::GetTime();
      ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
      for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
      {
        const float b = arc_angle * arc_num - PI_DIV_2 - PI_DIV_4;
        const float e = arc_angle * arc_num + arc_angle - PI_DIV_2 - PI_DIV_4;
        const float a = arc_angle * arc_num;
        float r = (0.6f + 0.3f * ImSin(start * (speed * rkoeff[arc_num % 6][2] * 2.f) + (2.f * rkoeff[arc_num % 6][0])));

        window->DrawList->PathClear();
        window->DrawList->PathLineTo(centre);
        ImColor c = ImColor::HSV(out_h + arc_num * 0.31f, out_s, out_v);
        for (size_t i = 0; i <= num_segments + 1; i++)
        {
          const float ar = arc_angle * arc_num + (i * angle_offset) - PI_DIV_2 - PI_DIV_4;
          window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ar) * (radius * r), centre.y + ImSin(ar) * (radius * r)));
        }
        window->DrawList->PathFillConvex(color_alpha(c, 1.f));
      }
    }

    inline void SpinnerCaleidoscope(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 6, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      float start = (float)ImGui::GetTime() * speed;
      float astart = ImFmod(start, PI_2 / arcs);
      start -= astart;
      const float angle_offset = PI_2 / arcs;
      const float angle_offset_t = angle_offset * 0.3f;
      arcs = ImMin<size_t>(arcs, 32);

      auto get_points = [&] (float left, float right, float r) -> std::array<ImVec2, 4> {
        const float rmin = r - thickness;
        return {
          ImVec2(centre.x + ImCos(left) * rmin, centre.y + ImSin(left) * rmin),
          ImVec2(centre.x + ImCos(left) * r, centre.y + ImSin(left) * r),
          ImVec2(centre.x + ImCos(right) * r, centre.y + ImSin(right) * r),
          ImVec2(centre.x + ImCos(right) * rmin, centre.y + ImSin(right) * rmin)
        };
      };

      auto draw_sectors = [&] (float s, const std::function<ImColor (size_t)>& color_func, float r) {
        for (size_t i = 0; i <= arcs; i++) {
          float left = s + (i * angle_offset) - angle_offset_t;
          float right = s + (i * angle_offset) + angle_offset_t;
          auto points = get_points(left, right, r);
          window->DrawList->AddConvexPolyFilled(points.data(), 4, color_alpha(color_func(i), 1.f));
        }
      };

      float out_h, out_s, out_v;
      ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
      draw_sectors(start, [&] (size_t i) { return ImColor::HSV(out_h + i * 0.31f, out_s, out_v); }, radius);
      switch (mode) {
      case 0: draw_sectors(-start * 0.78f, [&] (size_t i) { return ImColor::HSV(out_h + i * 0.31f, out_s, out_v); }, radius - thickness - 2); break;
      case 1:
        {
          ImColor c = color;
          float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
          c.Value.w = ImMax(0.1f, ImMin(lerp_koeff, 1.f));
          float dr = radius - thickness - 3;
          window->DrawList->AddCircleFilled(centre, dr, c, num_segments);
        } 
        break;
      }
    }





    inline void SpinnerSineArcs(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        float length = ImFmod(start, IM_PI);
        const float dangle = ImSin(length) * IM_PI * 0.35f;
        const float angle_offset = IM_PI / num_segments;

        auto draw_spring = [&] (float k) {
            float arc = 0.f;
            window->DrawList->PathClear();
            for (size_t i = 0; i < num_segments; i++) {
                float a = start + (i * angle_offset);
                if (ImSin(a) < 0.f)
                    a *= -1;
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + k * ImSin(a) * radius));
                arc += angle_offset;
                if (arc > dangle)
                    break;
            }
            detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);
        };

        draw_spring(1);
        draw_spring(-1);
    }

    inline void SpinnerTrianglesShift(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, size_t bars = 8)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        ImColor c = color;
        float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
        c.Value.w = ImMax(0.1f, ImMin(lerp_koeff, 1.f));

        const float angle_offset = PI_2 / bars;
        float start = (float)ImGui::GetTime() * speed;
        const float astart = ImFmod(start, angle_offset);
        const float save_start = start;
        start -= astart;
        const float angle_offset_t = angle_offset * 0.3f;
        bars = ImMin<size_t>(bars, 32);

        const float rmin = radius - thickness;
        auto get_points = [&] (float left, float right, float r1, float r2) -> std::array<ImVec2, 4> {
            return {
                ImVec2(centre.x + ImCos(left) * r1, centre.y + ImSin(left) * r1),
                ImVec2(centre.x + ImCos(left) * r2, centre.y + ImSin(left) * r2),
                ImVec2(centre.x + ImCos(right) * r2, centre.y + ImSin(right) * r2),
                ImVec2(centre.x + ImCos(right) * r1, centre.y + ImSin(right) * r1)
            };
        };

        ImColor rc = bg;
        for (size_t i = 0; i < bars; i++) {
            float left = start + (i * angle_offset) - angle_offset_t;
            float right = start + (i * angle_offset) + angle_offset_t;
            float centera = start - PI_DIV_2 + (i * angle_offset);
            float rmul = 1.f - ImClamp(ImAbs(centera - save_start), 0.f, PI_DIV_2) / PI_DIV_2;
            rc.Value.w = ImMax(rmul, 0.1f);
            rmul *= 1.5f;
            rmul = ImMax(0.5f, rmul);
            const float r1 = ImMax(rmin * rmul, rmin);
            const float r2 = ImMax(radius * rmul, radius);
            auto points = get_points(left, right, r1, r2);
            window->DrawList->AddConvexPolyFilled(points.data(), 4, color_alpha(rc, 1.f));
        }
    }

    inline void SpinnerPointsShift(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = half_white, float speed = 2.8f, size_t bars = 8)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        ImColor c = color;
        float lerp_koeff = (ImSin((float)ImGui::GetTime() * speed) + 1.f) * 0.5f;
        c.Value.w = ImMax(0.1f, ImMin(lerp_koeff, 1.f));

        const float angle_offset = PI_2 / bars;
        float start = (float)ImGui::GetTime() * speed;
        const float astart = ImFmod(start, angle_offset);
        const float save_start = start;
        start -= astart;
        const float angle_offset_t = angle_offset * 0.3f;
        bars = ImMin<size_t>(bars, 32);

        const float rmin = radius - thickness;

        ImColor rc = bg;
        for (size_t i = 0; i < bars; i++) {
            float left = start + (i * angle_offset) - angle_offset_t;
            float centera = start - PI_DIV_2 + (i * angle_offset);
            float rmul = 1.f - ImClamp(ImAbs(centera - save_start), 0.f, PI_DIV_2) / PI_DIV_2;
            rc.Value.w = ImMax(rmul, 0.1f);
            rmul *= 1.f + ImSin(rmul * IM_PI);
            const float r = ImMax(radius * rmul, radius);
            window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(left) * r, centre.y + ImSin(left) * r), thickness, color_alpha(rc, 1.f), num_segments);
        }
    }


    inline void SpinnerCircularPoints(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.8f, int lines = 8)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, radius);
        const float bg_angle_offset = (PI_2) / lines;
        for (size_t j = 0; j < 3; ++j)
        {
            const float start_offset = j * radius / 3.f;
            const float rmax = ImFmod((start + start_offset), radius);

            ImColor c = color_alpha(color, ImSin((radius - rmax) / radius * IM_PI));
            for (size_t i = 0; i < lines; i++)
            {
                float a = (i * bg_angle_offset);
                window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(a) * rmax, centre.y + ImSin(a) * rmax), thickness, c, num_segments);
            }
        }
    }

    inline void SpinnerCurvedCircle(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t circles = 1)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);
        const float bg_angle_offset = PI_2 / num_segments;

        float out_h, out_s, out_v;
        ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, out_h, out_s, out_v);
        for (int j = 0; j < circles; j++)
        {
            window->DrawList->PathClear();
            const float rr = radius - ((radius * 0.5f) / circles) * j;
            const float start_a = start * (1.1f * (j+1));
            for (size_t i = 0; i <= num_segments; i++)
            {
                const float a = start_a + (i * bg_angle_offset);
                const float r = rr - (0.2f * (i % 2)) * rr;
                window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * r, centre.y + ImSin(a) * r));
            }
            detail::PathStroke(window->DrawList, color_alpha(ImColor::HSV(out_h + (j * 1.f / circles), out_s, out_v), 1.f), thickness, false);
        }
    }

    inline void SpinnerModCircle(const char *label, float radius, float thickness, const ImColor &color = white, float ang_min = 1.f, float ang_max = 1.f, float speed = 2.8f)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        float start = ImFmod((float)ImGui::GetTime() * speed, PI_2);

        window->DrawList->PathClear();
        for (size_t i = 0; i <= 90; i++)
        {
            const float ax = ((i / 90.f) * PI_2 * ang_min);
            const float ay = ((i / 90.f) * PI_2 * ang_max);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(ax) * radius, centre.y + ImSin(ay) * radius));
        }
        detail::PathStroke(window->DrawList, color_alpha(color, 1.f), thickness, false);

        start = (start < IM_PI) ? (start * 2.f) : (PI_2 - start) * 2.f;
        window->DrawList->AddCircleFilled(ImVec2(centre.x + ImCos(start * ang_min) * radius, centre.y + ImSin(start * ang_max) * radius), thickness * 4.f, color_alpha(color, 1.f), num_segments);
    }



    inline void SpinnerRotateSegmentsPulsar(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 2.8f, size_t arcs = 4, size_t layers = 1)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);

        const float arc_angle = PI_2 / (float)arcs;
        const float angle_offset = arc_angle / num_segments;
        float r = radius;
        float reverse = 1.f;

        const float bg_angle_offset = PI_2_DIV(num_segments);
        const float koeff = PI_DIV(2 * layers);
        float start = (float)ImGui::GetTime() * speed;

        for (int num_ring = 0; num_ring < layers; ++num_ring) {
            float radius_k = ImSin(ImFmod(start + (num_ring * koeff), PI_DIV_2));
            ImColor c = color_alpha(color, (radius_k > 0.5f) ? (2.f - (radius_k * 2.f)) : color.Value.w);

            for (size_t arc_num = 0; arc_num < arcs; ++arc_num)
            {
                window->DrawList->PathClear();
                for (size_t i = 2; i <= num_segments - 2; i++)
                {
                    const float a = start * (1.f + 0.1f * num_ring) + arc_angle * arc_num + (i * angle_offset);
                    window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a * reverse) * (r * radius_k), centre.y + ImSin(a * reverse) * (r * radius_k)));
                }
                detail::PathStroke(window->DrawList, c, thickness, false);
            }
        }
    }

    inline void SpinnerSplineAng(const char *label, float radius, float thickness, const ImColor &color = white, const ImColor &bg = white, float speed = 2.8f, float angle = IM_PI, int mode = 0)
    {
        SPINNER_HEADER(pos, size, centre, num_segments);                            // Get the position, size, centre, and number of segments of the spinner using the SPINNER_HEADER macro.
        float start = (float)ImGui::GetTime() * speed;                        // The start angle of the spinner is calculated based on the current time and the specified speed.
        radius = (mode == 1) ? (0.8f + ImCos(start) * 0.2f) * radius : radius;

        //circle([&] (int i) {                                                         // Draw the background of the spinner using the `circle` function, with the specified background color and thickness.
        //    const float a = start + (i * (PI_2 / (num_segments - 1)));               // Calculate the angle for each segment based on the start angle and the number of segments.
        //    return ImVec2(ImCos(a) * (radius + thickness), damped_infinity(1.3f, (float)a).second * radius);
        //}, color_alpha(bg, 1.f), thickness);

        const float b = damped_gravity(ImSin(start * 1.1f)) * angle;
        circle([&] (int i) {                                                        // Draw the spinner itself using the `circle` function, with the specified color and thickness.
            const float a = start - b + (i * angle / num_segments);
            return ImVec2(ImSin(a) * radius, ImCos(a) * radius);
        }, color_alpha(color, 1.f), thickness);
    }

    // Conic grid:
    //   a 2x2 block of conic-corner tiles (split by a "+" gap) with five dots at
    //   the edge midpoints and the centre; the whole figure spins .5 turn / sec
    //   (linear). Port of the CSS "l8" loader. mode 2 reverses the spin; mode 1
    //   doubles it to a full turn.
    inline void SpinnerConicGrid(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);
      (void)thickness;

      const float W = radius * 2.f, H = W;
      const float g = 0.06f * W;            // half of the "+" gap between tiles
      const float dot_r = 0.12f * W;        // 12px dot on a 50px box
      const ImColor c = color_alpha(color, 1.f);

      const float time = (float)ImGui::GetTime() * speed;
      float ang = ImFmod(time, 1.f) * IM_PI;        // .5 turn per second
      if (mode == 1) ang *= 2.f;                    // full turn
      if (mode == 2) ang = -ang;
      const float ca = ImCos(ang), sa = ImSin(ang);

      auto rot = [&](float x, float y) -> ImVec2 {
        const float dx = x - centre.x, dy = y - centre.y;
        return ImVec2(centre.x + dx * ca - dy * sa, centre.y + dx * sa + dy * ca);
      };

      const float l = centre.x - radius, t = centre.y - radius, r = l + W, b = t + H;
      const float cx = centre.x, cy = centre.y;

      // four quadrant tiles, separated by the central "+" gap
      const ImVec2 tiles[4][4] = {
        { {l, t},       {cx - g, t},    {cx - g, cy - g}, {l, cy - g} },     // top-left
        { {cx + g, t},  {r, t},         {r, cy - g},      {cx + g, cy - g} },// top-right
        { {cx + g, cy + g}, {r, cy + g}, {r, b},          {cx + g, b} },     // bottom-right
        { {l, cy + g},  {cx - g, cy + g}, {cx - g, b},    {l, b} },          // bottom-left
      };
      for (int i = 0; i < 4; i++)
        window->DrawList->AddQuadFilled(rot(tiles[i][0].x, tiles[i][0].y), rot(tiles[i][1].x, tiles[i][1].y),
                                        rot(tiles[i][2].x, tiles[i][2].y), rot(tiles[i][3].x, tiles[i][3].y), c);

      // five dots: edge midpoints (inset by the radius) and the centre
      const ImVec2 dots[5] = {
        { cx,            t + dot_r },     // top
        { cx,            b - dot_r },     // bottom
        { r - dot_r,     cy },            // right
        { l + dot_r,     cy },            // left
        { cx,            cy },            // centre
      };
      for (int i = 0; i < 5; i++) {
        const ImVec2 p = rot(dots[i].x, dots[i].y);
        window->DrawList->AddCircleFilled(p, dot_r, c, num_segments);
      }
    }

    // Arc arrow:
    //   a 270-degree ring arc with a triangular arrowhead at its leading tip,
    //   spinning a full turn / sec (linear). Port of the CSS "l11" loader.
    //   mode 1 drops the arrowhead (plain 3/4 ring); mode 2 reverses the spin.
    inline void SpinnerArcArrow(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);

      const float R = radius - thickness * 0.5f;
      const ImColor c = color_alpha(color, 1.f);

      const float time = (float)ImGui::GetTime() * speed;
      float rot = ImFmod(time, 1.f) * (2.f * IM_PI);   // full turn per second
      if (mode == 2) rot = -rot;

      const float a0 = -IM_PI * 0.5f + rot;            // start at the top
      const float a1 = a0 + IM_PI * 1.5f;              // 270-degree sweep -> left end
      window->DrawList->PathArcTo(centre, R, a0, a1, num_segments * 2);
      detail::PathStroke(window->DrawList, c, thickness, false);

      if (mode != 1) {                                 // arrowhead at the leading end
        const float ca = ImCos(a1), sa = ImSin(a1);
        const float aw = thickness * 1.3f, al = thickness * 2.2f;
        const ImVec2 tip (centre.x + ca * R - sa * al, centre.y + sa * R + ca * al);
        const ImVec2 bout(centre.x + ca * (R + aw),    centre.y + sa * (R + aw));
        const ImVec2 bin (centre.x + ca * (R - aw),    centre.y + sa * (R - aw));
        window->DrawList->AddTriangleFilled(tip, bout, bin, c);
      }
    }

    // Orbit moon:
    //   an orange hub at the centre, a green planet orbiting it once per second,
    //   and a small grey moon circling the planet twice as fast. Port of the CSS
    //   "l17" loader (its three signature colours are fixed). mode 2 reverses.
    inline void SpinnerOrbitMoon(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);
      (void)thickness; (void)color;

      const float s = radius / 35.f;          // CSS box is 70px (half = 35px)
      const ImColor orange = color_alpha(ImColor(255, 165, 22), 1.f);
      const ImColor green  = color_alpha(ImColor(0, 128, 0), 1.f);
      const ImColor grey   = color_alpha(ImColor(204, 204, 204), 1.f);

      const float time = (float)ImGui::GetTime() * speed;
      float th = ImFmod(time, 1.f) * (2.f * IM_PI);            // hub orbit: 1 rev/sec
      float ph = ImFmod(time, 0.5f) / 0.5f * (2.f * IM_PI);    // moon orbit: 2 rev/sec
      if (mode == 2) { th = -th; ph = -ph; }

      auto rot = [](float x, float y, float a, float &ox, float &oy) {
        const float ca = ImCos(a), sa = ImSin(a);
        ox = x * ca - y * sa; oy = x * sa + y * ca;
      };

      // planet (green) orbits the hub, starting at the bottom
      float gx, gy;
      rot(0.f, 29.f * s, th, gx, gy);
      window->DrawList->AddCircleFilled(ImVec2(centre.x + gx, centre.y + gy), 6.f * s, green, num_segments);

      // hub (orange) sits at the centre
      window->DrawList->AddCircleFilled(centre, 8.f * s, orange, num_segments);

      // moon (grey): circle the planet, then carry the whole arm around the hub
      float mlx, mly;
      rot(0.f, -14.f * s, ph, mlx, mly);
      float mx, my;
      rot(mlx, 29.f * s + mly, th, mx, my);
      window->DrawList->AddCircleFilled(ImVec2(centre.x + mx, centre.y + my), 4.f * s, grey, num_segments);
    }

    // Conic wheels:
    //   three concentric four-colour pie discs spinning at different rates. In
    //   CSS the parent's spin compounds onto the inner discs, giving effective
    //   periods of 2s / 1s / 1.2s per turn. Port of the CSS "l22" loader (its
    //   four signature colours are fixed). mode 2 reverses the spin.
    inline void SpinnerConicWheels(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);
      (void)thickness; (void)color;

      const ImColor cols[4] = {
        color_alpha(ImColor(37, 176, 155), 1.f),   // #25b09b teal
        color_alpha(ImColor(240, 51, 85), 1.f),    // #f03355 red
        color_alpha(ImColor(81, 75, 130), 1.f),    // #514b82 purple
        color_alpha(ImColor(255, 165, 22), 1.f),   // #ffa516 orange
      };

      const float time = (float)ImGui::GetTime() * speed;
      float aO = ImFmod(time, 2.f) / 2.f * (2.f * IM_PI);     // outer:  2.0s / turn
      float aM = ImFmod(time, 1.f) * (2.f * IM_PI);           // middle: 1.0s / turn
      float aI = ImFmod(time, 1.2f) / 1.2f * (2.f * IM_PI);   // inner:  1.2s / turn
      if (mode == 2) { aO = -aO; aM = -aM; aI = -aI; }

      auto draw_disk = [&](float r, float a) {
        for (int i = 0; i < 4; i++) {
          const float a0 = -IM_PI * 0.5f + a + i * (IM_PI * 0.5f);   // conic starts at top, clockwise
          window->DrawList->PathArcTo(centre, r, a0, a0 + IM_PI * 0.5f, num_segments);
          window->DrawList->PathLineTo(centre);
          window->DrawList->PathFillConvex(cols[i]);
        }
      };

      draw_disk(radius, aO);              // outer disc painted first
      draw_disk(radius * 0.7f, aM);       // middle disc (margin 15%)
      draw_disk(radius * 0.5f, aI);       // inner disc (margin 25%)
    }

    // Dot ring:
    //   twelve dots evenly spaced (30 deg apart) on a circle, alternating two
    //   colours, the whole ring spinning one turn / 2 sec counter-clockwise.
    //   Port of the CSS "l30" loader (its two signature colours are fixed).
    //   mode 2 reverses the spin.
    inline void SpinnerDotRing(const char *label, float radius, float thickness, const ImColor &color = white, float speed = 1.f, int mode = 0)
    {
      SPINNER_HEADER(pos, size, centre, num_segments);
      (void)color;

      const ImColor g1 = color_alpha(ImColor(81, 75, 130), 1.f);    // #514b82 purple
      const ImColor g2 = color_alpha(ImColor(238, 238, 238), 1.f);  // #eeeeee light grey
      const int N = 12;
      const float ring = radius - thickness;

      const float time = (float)ImGui::GetTime() * speed;
      float spin = ImFmod(time, 2.f) / 2.f * (2.f * IM_PI);   // one turn per 2 sec
      spin = (mode == 2) ? spin : -spin;                      // CSS rotates -1turn (CCW)

      for (int k = 0; k < N; k++) {
        const float a = -IM_PI * 0.5f + spin + k * (2.f * IM_PI / N);
        const ImVec2 p(centre.x + ImCos(a) * ring, centre.y + ImSin(a) * ring);
        window->DrawList->AddCircleFilled(p, thickness, (k % 2 == 0) ? g1 : g2, num_segments);
      }
    }

}

#endif // _IMSPINNER_H_
