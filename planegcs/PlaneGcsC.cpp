// PlaneGcsC: a flat C API over planegcs (FreeCAD's sketch solver) for CAD Editor.
// Copyright (c) 2026 KoykStudio. This file is compiled into PlaneGcs.dll together with planegcs, which is under the
// GNU Lesser General Public License version 2 or later; this wrapper is distributed under the same licence
// (LGPL 2.1 or later). Its complete source, the planegcs sources, our patch and the build script are published with
// the library (see README.md).
//
// pg_solve(json) -> json: one solve of a whole sketch. The request lists parameter values, geometry made of those
// parameters, and constraints; the answer gives the solved values, the degrees of freedom and the conflicting and
// redundant constraints (by tag). Nothing is kept between calls.
//
// request: {
//   "params": [v…],                      every number: coordinates, radii, angles (radians), dimension values
//   "fixed": [i…],                       parameters that stay as they are (dimension values, dragged points, the origin)
//   "geos": [ {"k":"point","p":[x,y]} | {"k":"line","p1":[x,y],"p2":[x,y]} | {"k":"circle","c":[x,y],"r":i}
//           | {"k":"arc","c":[x,y],"r":i,"a0":i,"a1":i,"s":[x,y],"e":[x,y]}
//           | {"k":"ellipse","c":[x,y],"f1":[x,y],"rmin":i} ],         (x, y, r … are parameter indices)
//   "cons": [ {"k": kind, "tag": n, "a": {"g": geo, "pos": 0 edge|1 start|2 end|3 centre}, "b": …, "c": …, "v": i} ],
//   "diagnose": true,                    also compute DOF and conflicts (slower; skipped while dragging)
// }
// answer: { "status": "ok"|"converged"|"failed", "params": [v…], "dof": n, "conflicting": [tag…], "redundant": [tag…],
//           "partiallyRedundant": [tag…], "ms": t }
#define _USE_MATH_DEFINES   // M_PI with MSVC
#include <planegcs/GCS.h>  // the patched copy in the build folder

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <deque>

#include "../OcctModel/third_party/nlohmann/json.hpp"

#ifdef _WIN32
#define PG_API extern "C" __declspec(dllexport)
#else
#define PG_API extern "C" __attribute__((visibility("default")))
#endif

using json = nlohmann::json;

namespace
{

struct Sys
{
    std::vector<double> params;
    std::deque<double> fixedAngles;   // angles held constant (tangency via a point); a deque keeps their addresses
    std::vector<std::unique_ptr<GCS::Point>> points;
    std::vector<std::unique_ptr<GCS::Line>> lines;
    std::vector<std::unique_ptr<GCS::Circle>> circles;
    std::vector<std::unique_ptr<GCS::Arc>> arcs;
    std::vector<std::unique_ptr<GCS::Ellipse>> ellipses;
    struct Geo
    {
        std::string kind;
        GCS::Point* point = nullptr;
        GCS::Line* line = nullptr;
        GCS::Circle* circle = nullptr;  // also set for arcs (an Arc is a Circle)
        GCS::Arc* arc = nullptr;
        GCS::Ellipse* ellipse = nullptr;
    };
    std::vector<Geo> geos;
    GCS::System sys;

    double* P(const json& i)
    {
        size_t k = i.get<size_t>();
        if (k >= params.size()) throw std::runtime_error("parameter index out of range");
        return &params[k];
    }

    GCS::Point Pt(const json& xy) { GCS::Point p; p.x = P(xy.at(0)); p.y = P(xy.at(1)); return p; }

    // The point of a geometry a constraint refers to.
    GCS::Point& PointOf(const json& r)
    {
        auto& g = geos.at(r.at("g").get<size_t>());
        int pos = r.value("pos", 0);
        if (g.point) return *g.point;
        if (g.line) {
            if (pos == 1) return g.line->p1;
            if (pos == 2) return g.line->p2;
        }
        if (g.arc) {
            if (pos == 1) return g.arc->start;
            if (pos == 2) return g.arc->end;
            if (pos == 3) return g.arc->center;
        }
        if (g.circle && pos == 3) return g.circle->center;
        if (g.ellipse && pos == 3) return g.ellipse->center;
        throw std::runtime_error("this element has no such point");
    }

    GCS::Line& LineOf(const json& r)
    {
        auto& g = geos.at(r.at("g").get<size_t>());
        if (!g.line) throw std::runtime_error("a line is needed");
        return *g.line;
    }

    bool IsPoint(const json& r)
    {
        auto& g = geos.at(r.at("g").get<size_t>());
        return g.point || r.value("pos", 0) != 0;
    }

    Geo& G(const json& r) { return geos.at(r.at("g").get<size_t>()); }
};

void Build(Sys& s, const json& req)
{
    for (auto& v : req.at("params")) s.params.push_back(v.get<double>());
    for (auto& g : req.at("geos")) {
        Sys::Geo geo;
        geo.kind = g.at("k");
        if (geo.kind == "point") {
            s.points.push_back(std::make_unique<GCS::Point>(s.Pt(g.at("p"))));
            geo.point = s.points.back().get();
        }
        else if (geo.kind == "line") {
            auto l = std::make_unique<GCS::Line>();
            l->p1 = s.Pt(g.at("p1"));
            l->p2 = s.Pt(g.at("p2"));
            geo.line = l.get();
            s.lines.push_back(std::move(l));
        }
        else if (geo.kind == "circle") {
            auto c = std::make_unique<GCS::Circle>();
            c->center = s.Pt(g.at("c"));
            c->rad = s.P(g.at("r"));
            geo.circle = c.get();
            s.circles.push_back(std::move(c));
        }
        else if (geo.kind == "arc") {
            auto a = std::make_unique<GCS::Arc>();
            a->center = s.Pt(g.at("c"));
            a->rad = s.P(g.at("r"));
            a->startAngle = s.P(g.at("a0"));
            a->endAngle = s.P(g.at("a1"));
            a->start = s.Pt(g.at("s"));
            a->end = s.Pt(g.at("e"));
            geo.arc = a.get();
            geo.circle = a.get();
            s.arcs.push_back(std::move(a));
        }
        else if (geo.kind == "ellipse") {
            auto e = std::make_unique<GCS::Ellipse>();
            e->center = s.Pt(g.at("c"));
            e->focus1 = s.Pt(g.at("f1"));
            e->radmin = s.P(g.at("rmin"));
            geo.ellipse = e.get();
            s.ellipses.push_back(std::move(e));
        }
        else {
            throw std::runtime_error("unknown geometry " + geo.kind);
        }
        s.geos.push_back(geo);
    }
    // Arcs keep their end points on the circle at their angles.
    for (auto& a : s.arcs) {
        s.sys.addConstraintArcRules(*a);
    }
    for (auto& c : req.at("cons")) {
        std::string k = c.at("k");
        int tag = c.value("tag", 0);
        auto& sys = s.sys;
        const json& a = c.at("a");
        auto V = [&]() { return s.P(c.at("v")); };
        if (k == "coincident") {
            sys.addConstraintP2PCoincident(s.PointOf(a), s.PointOf(c.at("b")), tag);
        }
        else if (k == "horizontal" || k == "vertical") {
            bool h = k == "horizontal";
            if (c.contains("b")) {
                if (h) sys.addConstraintHorizontal(s.PointOf(a), s.PointOf(c["b"]), tag);
                else sys.addConstraintVertical(s.PointOf(a), s.PointOf(c["b"]), tag);
            }
            else if (h) sys.addConstraintHorizontal(s.LineOf(a), tag);
            else sys.addConstraintVertical(s.LineOf(a), tag);
        }
        else if (k == "parallel") {
            sys.addConstraintParallel(s.LineOf(a), s.LineOf(c.at("b")), tag);
        }
        else if (k == "perpendicular") {
            sys.addConstraintPerpendicular(s.LineOf(a), s.LineOf(c.at("b")), tag);
        }
        else if (k == "tangent") {
            auto& A = s.G(a);
            auto& B = s.G(c.at("b"));
            if (A.line && B.arc) sys.addConstraintTangent(*A.line, *B.arc, tag);
            else if (B.line && A.arc) sys.addConstraintTangent(*B.line, *A.arc, tag);
            else if (A.line && B.circle) sys.addConstraintTangent(*A.line, *B.circle, tag);
            else if (B.line && A.circle) sys.addConstraintTangent(*B.line, *A.circle, tag);
            else if (A.line && B.ellipse) sys.addConstraintTangent(*A.line, *B.ellipse, tag);
            else if (B.line && A.ellipse) sys.addConstraintTangent(*B.line, *A.ellipse, tag);
            else if (A.arc && B.arc) sys.addConstraintTangent(*A.arc, *B.arc, tag);
            else if (A.arc && B.circle) sys.addConstraintTangent(*B.circle, *A.arc, tag);
            else if (A.circle && B.arc) sys.addConstraintTangent(*A.circle, *B.arc, tag);
            else if (A.circle && B.circle) sys.addConstraintTangent(*A.circle, *B.circle, tag);
            else throw std::runtime_error("tangent needs lines, circles, arcs or ellipses");
        }
        else if (k == "tangentAt") {
            // Tangent at a shared point (a fillet arc meeting a line, a slot's ends): the curves' normals at the point
            // are parallel. Coincident ends plus an edge-to-edge tangent is degenerate there (the solver counts the
            // tangent as redundant and reports degrees of freedom that aren't there); this is how FreeCAD does it.
            auto curve = [&](Sys::Geo& g) -> GCS::Curve* {
                if (g.line) return g.line;
                if (g.arc) return g.arc;
                if (g.circle) return g.circle;
                if (g.ellipse) return g.ellipse;
                return nullptr;
            };
            auto* A = curve(s.G(a));
            auto* B = curve(s.G(c.at("b")));
            if (!A || !B) throw std::runtime_error("tangent needs lines, circles, arcs or ellipses");
            auto& pt = s.PointOf(c.at("p"));
            double ang = sys.calculateAngleViaPoint(*A, *B, pt);
            s.fixedAngles.push_back(ang > M_PI / 2 ? M_PI : ang < -M_PI / 2 ? -M_PI : 0.0);
            sys.addConstraintAngleViaPoint(*A, *B, pt, &s.fixedAngles.back(), tag);
        }
        else if (k == "equal") {
            auto& A = s.G(a);
            auto& B = s.G(c.at("b"));
            if (A.line && B.line) sys.addConstraintEqualLength(*A.line, *B.line, tag);
            else if (A.arc && B.arc) sys.addConstraintEqualRadius(*A.arc, *B.arc, tag);
            else if (A.circle && B.arc) sys.addConstraintEqualRadius(*A.circle, *B.arc, tag);
            else if (A.arc && B.circle) sys.addConstraintEqualRadius(*B.circle, *A.arc, tag);
            else if (A.circle && B.circle) sys.addConstraintEqualRadius(*A.circle, *B.circle, tag);
            else if (A.ellipse && B.ellipse) sys.addConstraintEqualRadii(*A.ellipse, *B.ellipse, tag);
            else throw std::runtime_error("equal needs two lines or two circles/arcs");
        }
        else if (k == "symmetric") {
            // a and b symmetric about c: a line (mirror) or a point (centre).
            auto& C = c.at("c");
            if (!s.IsPoint(C) && s.G(C).line) sys.addConstraintP2PSymmetric(s.PointOf(a), s.PointOf(c.at("b")), *s.G(C).line, tag);
            else sys.addConstraintP2PSymmetric(s.PointOf(a), s.PointOf(c.at("b")), s.PointOf(C), tag);
        }
        else if (k == "midpoint") {
            // point a at the middle of line b: the line's ends are symmetric about the point.
            auto& L = s.LineOf(c.at("b"));
            sys.addConstraintP2PSymmetric(L.p1, L.p2, s.PointOf(a), tag);
        }
        else if (k == "pointOn") {
            auto& B = s.G(c.at("b"));
            if (B.line) sys.addConstraintPointOnLine(s.PointOf(a), *B.line, tag);
            else if (B.arc) sys.addConstraintPointOnArc(s.PointOf(a), *B.arc, tag);
            else if (B.circle) sys.addConstraintPointOnCircle(s.PointOf(a), *B.circle, tag);
            else if (B.ellipse) sys.addConstraintPointOnEllipse(s.PointOf(a), *B.ellipse, tag);
            else throw std::runtime_error("point on object needs a line, circle, arc or ellipse");
        }
        else if (k == "coordX") {
            sys.addConstraintCoordinateX(s.PointOf(a), V(), tag);
        }
        else if (k == "coordY") {
            sys.addConstraintCoordinateY(s.PointOf(a), V(), tag);
        }
        else if (k == "distance") {
            if (c.contains("b")) {
                auto& B = c.at("b");
                if (s.IsPoint(B)) sys.addConstraintP2PDistance(s.PointOf(a), s.PointOf(B), V(), tag);
                else if (s.G(B).line) sys.addConstraintP2LDistance(s.PointOf(a), *s.G(B).line, V(), tag);
                else if (s.G(B).circle) sys.addConstraintP2CDistance(s.PointOf(a), *s.G(B).circle, V(), tag);
                else throw std::runtime_error("distance to this element isn't supported");
            }
            else {
                auto& L = s.LineOf(a);  // a line's length
                sys.addConstraintP2PDistance(L.p1, L.p2, V(), tag);
            }
        }
        else if (k == "distanceX" || k == "distanceY") {
            bool x = k == "distanceX";
            GCS::Point *p1, *p2;
            if (c.contains("b")) { p1 = &s.PointOf(a); p2 = &s.PointOf(c["b"]); }
            else { auto& L = s.LineOf(a); p1 = &L.p1; p2 = &L.p2; }
            sys.addConstraintDifference(x ? p1->x : p1->y, x ? p2->x : p2->y, V(), tag);
        }
        else if (k == "radius" || k == "diameter") {
            auto& A = s.G(a);
            bool r = k == "radius";
            if (A.arc) { if (r) sys.addConstraintArcRadius(*A.arc, V(), tag); else sys.addConstraintArcDiameter(*A.arc, V(), tag); }
            else if (A.circle) { if (r) sys.addConstraintCircleRadius(*A.circle, V(), tag); else sys.addConstraintCircleDiameter(*A.circle, V(), tag); }
            else throw std::runtime_error("radius needs a circle or an arc");
        }
        else if (k == "angle") {
            // Between two lines (radians, from a to b), or a line against the x axis.
            if (c.contains("b")) sys.addConstraintL2LAngle(s.LineOf(a), s.LineOf(c["b"]), V(), tag);
            else { auto& L = s.LineOf(a); sys.addConstraintP2PAngle(L.p1, L.p2, V(), tag); }
        }
        else {
            throw std::runtime_error("unknown constraint " + k);
        }
    }
}

char* Dup(const std::string& t)
{
    char* p = static_cast<char*>(std::malloc(t.size() + 1));
    if (p) std::memcpy(p, t.c_str(), t.size() + 1);
    return p;
}

}  // namespace

PG_API int pg_solve(const char* request, char** out)
{
    json result;
    int ok = 0;
    try {
        auto t0 = std::chrono::steady_clock::now();
        json req = json::parse(request);
        auto s = std::make_unique<Sys>();
        Build(*s, req);
        std::vector<bool> fixed(s->params.size(), false);
        if (req.contains("fixed")) for (auto& i : req["fixed"]) fixed.at(i.get<size_t>()) = true;
        GCS::VEC_pD unknowns;
        for (size_t i = 0; i < s->params.size(); i++) if (!fixed[i]) unknowns.push_back(&s->params[i]);
        auto tb = std::chrono::steady_clock::now();
        s->sys.declareUnknowns(unknowns);
        s->sys.initSolution(GCS::DogLeg);
        auto ti = std::chrono::steady_clock::now();
        int status = s->sys.solve(true, GCS::DogLeg);
        auto ts = std::chrono::steady_clock::now();
        result["msBuild"] = std::chrono::duration<double, std::milli>(tb - t0).count();
        result["msInit"] = std::chrono::duration<double, std::milli>(ti - tb).count();
        result["msSolve"] = std::chrono::duration<double, std::milli>(ts - ti).count();
        if (status != GCS::Success) {
            s->sys.undoSolution();
            s->sys.initSolution(GCS::LevenbergMarquardt);
            status = s->sys.solve(true, GCS::LevenbergMarquardt);
        }
        if (status != GCS::Success) {
            s->sys.undoSolution();
            s->sys.initSolution(GCS::BFGS);
            status = s->sys.solve(true, GCS::BFGS);
        }
        if (status == GCS::Success || status == GCS::Converged) s->sys.applySolution();
        else s->sys.undoSolution();
        result["status"] = status == GCS::Success ? "ok" : status == GCS::Converged ? "converged" : "failed";
        result["params"] = s->params;
        if (req.value("diagnose", false)) {
            s->sys.diagnose(GCS::DogLeg);
            GCS::VEC_I conflicting, redundant, partial;
            s->sys.getConflicting(conflicting);
            s->sys.getRedundant(redundant);
            s->sys.getPartiallyRedundant(partial);
            result["dof"] = s->sys.dofsNumber();
            result["conflicting"] = conflicting;
            result["redundant"] = redundant;
            result["partiallyRedundant"] = partial;
        }
        result["ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ok = 1;
    }
    catch (std::exception& e) {
        result = {{"status", "error"}, {"error", e.what()}};
    }
    catch (...) {
        result = {{"status", "error"}, {"error", "unexpected error"}};
    }
    if (out) *out = Dup(result.dump());
    return ok;
}

// ---- drag sessions: the system is built and diagnosed once per drag gesture; each step only solves ----------------

namespace
{
struct Session
{
    std::unique_ptr<Sys> s;
    std::vector<size_t> dragged;   // parameter indices held at the pointer
    std::vector<double> lastGood;
};
}  // namespace

// Starts a drag: the request is as for pg_solve, plus "drag": [parameter indices] held fixed (and moved by
// pg_drag_step). Returns a session handle (null on error, with *out holding the error).
PG_API void* pg_drag_begin(const char* request, char** out)
{
    try {
        json req = json::parse(request);
        auto ses = std::make_unique<Session>();
        ses->s = std::make_unique<Sys>();
        Build(*ses->s, req);
        std::vector<bool> fixed(ses->s->params.size(), false);
        if (req.contains("fixed")) for (auto& i : req["fixed"]) fixed.at(i.get<size_t>()) = true;
        for (auto& i : req.at("drag")) { fixed.at(i.get<size_t>()) = true; ses->dragged.push_back(i.get<size_t>()); }
        GCS::VEC_pD unknowns;
        for (size_t i = 0; i < ses->s->params.size(); i++) if (!fixed[i]) unknowns.push_back(&ses->s->params[i]);
        ses->s->sys.declareUnknowns(unknowns);
        ses->s->sys.initSolution(GCS::DogLeg);
        ses->lastGood = ses->s->params;
        if (out) *out = Dup(json({{"status", "ok"}}).dump());
        return ses.release();
    }
    catch (std::exception& e) {
        if (out) *out = Dup(json({{"status", "error"}, {"error", e.what()}}).dump());
        return nullptr;
    }
}

// One drag step: the dragged parameters get `values` (in the order given to pg_drag_begin), then the rest is solved.
// On failure the sketch stays at the last good step. Writes the solved parameters into `params` (all of them).
PG_API int pg_drag_step(void* handle, const double* values, double* params, int count)
{
    auto* ses = static_cast<Session*>(handle);
    if (!ses || count != int(ses->s->params.size())) return -1;
    try {
        for (size_t k = 0; k < ses->dragged.size(); k++) ses->s->params[ses->dragged[k]] = values[k];
        int status = ses->s->sys.solve(true, GCS::DogLeg);
        if (status != GCS::Success) status = ses->s->sys.solve(true, GCS::LevenbergMarquardt);
        bool ok = status == GCS::Success || status == GCS::Converged;
        if (ok) { ses->s->sys.applySolution(); ses->lastGood = ses->s->params; }
        else ses->s->params = ses->lastGood;
        std::memcpy(params, ses->s->params.data(), sizeof(double) * ses->s->params.size());
        return ok ? 1 : 0;
    }
    catch (...) {
        ses->s->params = ses->lastGood;
        std::memcpy(params, ses->s->params.data(), sizeof(double) * ses->s->params.size());
        return 0;
    }
}

PG_API void pg_drag_end(void* handle)
{
    delete static_cast<Session*>(handle);
}

PG_API void pg_free(void* p)
{
    std::free(p);
}

PG_API const char* pg_version()
{
    return "planegcs from FreeCAD 1.1.4 (commit 4fd3bf320d95), KoykStudio patch 1";
}
