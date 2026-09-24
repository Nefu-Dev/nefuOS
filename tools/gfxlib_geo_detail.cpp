#include "gfxlib/geo.h"
#include <cstdio>
using namespace nefu::gfxlib;
int main() {
    Rect r = {2, 2, 8, 8};
    printf("rect-in %d %d\n", point_in_rect(5,5,r), point_in_rect(2,2,r));
    Circle c = {10, 10, 5};
    printf("circle-in %d edge %d out %d\n", point_in_circle(12,12,c), point_in_circle(15,10,c), point_in_circle(16,10,c));
    Rect a = {0,0,5,5}, b = {4,4,5,5}, far = {20,20,2,2};
    printf("rect-overlap %d %d disj %d\n", rect_overlap(a,b), rect_overlap(b,a), !rect_overlap(a,far));
    Circle c2 = {14,10,4}, c3 = {40,40,3};
    printf("cir-overlap %d disj %d\n", circle_overlap(c,c2), !circle_overlap(c,c3));
    printf("cir-rect %d no %d\n", circle_rect_overlap(c,b), !circle_rect_overlap(c,far));
    printf("dist2 %lld seg-end %lld seg-on %lld\n", (long long)dist2(0,0,3,4), (long long)point_segment_dist2(0,0,2,2,8,2), (long long)point_segment_dist2(5,2,2,2,8,2));
    Point pa={0,0}, pb={1,1}, pc={2,0}, pd={1,-1};
    printf("orient-cw %lld ccw %lld\n", (long long)orient(pa,pb,pc), (long long)orient(pa,pb,pd));
    Point s1a={0,0}, s1b={4,4}, s2a={0,4}, s2b={4,0}, s3a={1,5}, s3b={3,5};
    printf("seg-cross %d nocross %d\n", segments_intersect(s1a,s1b,s2a,s2b), !segments_intersect(s1a,s1b,s3a,s3b));
    Point t1={0,0}, t2={4,0}, t3={2,0}, t4={6,0};
    printf("seg-col %d\n", segments_intersect(t1,t2,t3,t4));
    Point li;
    bool liok = line_intersection(s1a,s1b,s2a,s2b,&li);
    printf("line-int %d pt %d,%d\n", liok, li.x, li.y);
    Point par1={0,0}, par2={1,0}, par3={0,1}, par4={1,1};
    printf("line-par %d\n", !line_intersection(par1,par2,par3,par4,&li));
    Point ta={0,0}, tb={4,0}, tc={0,3};
    printf("tri-area2 %lld in %d out %d\n", (long long)triangle_area2(ta,tb,tc), point_in_triangle({1,1},ta,tb,tc), !point_in_triangle({3,3},ta,tb,tc));
    Point sq[4] = {{0,0},{8,0},{8,8},{0,8}};
    printf("poly-area2 %lld\n", (long long)polygon_area2(sq,4));
    Point cg = polygon_centroid(sq,4);
    printf("centroid %d,%d in %d out %d\n", cg.x, cg.y, point_in_polygon({4,4},sq,4), !point_in_polygon({9,9},sq,4));
    Point L[6] = {{0,0},{6,0},{6,6},{2,6},{2,2},{0,2}};
    printf("L-in %d L-notch %d\n", point_in_polygon({1,1},L,6), !point_in_polygon({4,4},L,6));
    Point pts[5] = {{0,0},{10,0},{10,10},{0,10},{5,5}};
    Point hull[8];
    int hn = convex_hull(pts, 5, hull);
    printf("hull %d:", hn);
    for (int i = 0; i < hn; i++) printf(" (%d,%d)", hull[i].x, hull[i].y);
    printf("\n");
    Point d2[2] = {{0,0},{1,1}};
    printf("hull-degen %d\n", convex_hull(d2,2,hull));
    return 0;
}
