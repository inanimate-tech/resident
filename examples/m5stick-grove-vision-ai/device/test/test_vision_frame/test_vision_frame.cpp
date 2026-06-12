#include <unity.h>
#include "../../lib/vision/src/vision_frame.h"

void setUp(void) {}
void tearDown(void) {}

void test_classify_precedence(void) {
    using vision::Kind;
    // keypoints beat boxes beat points beat classes
    TEST_ASSERT_EQUAL(Kind::Pose,    vision::classify(1, 2, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Boxes,   vision::classify(0, 2, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Points,  vision::classify(0, 0, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Classes, vision::classify(0, 0, 0, 4));
    TEST_ASSERT_EQUAL(Kind::None,    vision::classify(0, 0, 0, 0));
}

void test_kind_name(void) {
    TEST_ASSERT_EQUAL_STRING("pose",    vision::kindName(vision::Kind::Pose));
    TEST_ASSERT_EQUAL_STRING("boxes",   vision::kindName(vision::Kind::Boxes));
    TEST_ASSERT_EQUAL_STRING("points",  vision::kindName(vision::Kind::Points));
    TEST_ASSERT_EQUAL_STRING("classes", vision::kindName(vision::Kind::Classes));
    TEST_ASSERT_EQUAL_STRING("none",    vision::kindName(vision::Kind::None));
}

void test_frame_count_follows_kind(void) {
    vision::Frame f;
    f.boxes = {{10, 20, 30, 40, 90, 1}, {50, 60, 70, 80, 70, 0}};
    f.classes = {{0, 99}};
    f.kind = vision::Kind::Boxes;
    TEST_ASSERT_EQUAL_INT(2, f.count());
    f.kind = vision::Kind::Classes;
    TEST_ASSERT_EQUAL_INT(1, f.count());
    f.kind = vision::Kind::None;
    TEST_ASSERT_EQUAL_INT(0, f.count());
}

void test_best_index_picks_highest_score(void) {
    vision::Frame f;
    f.kind = vision::Kind::Boxes;
    f.boxes = {{0, 0, 1, 1, 50, 0}, {0, 0, 1, 1, 95, 2}, {0, 0, 1, 1, 70, 1}};
    TEST_ASSERT_EQUAL_INT(1, vision::bestIndex(f));
}

void test_best_index_pose_uses_box_score(void) {
    vision::Frame f;
    f.kind = vision::Kind::Pose;
    vision::Person a; a.box = {0, 0, 1, 1, 40, 0};
    vision::Person b; b.box = {0, 0, 1, 1, 88, 0};
    f.people = {a, b};
    TEST_ASSERT_EQUAL_INT(1, vision::bestIndex(f));
}

void test_best_index_empty_is_minus_one(void) {
    vision::Frame f;
    TEST_ASSERT_EQUAL_INT(-1, vision::bestIndex(f));
    f.kind = vision::Kind::Boxes;  // kind set but vector empty
    TEST_ASSERT_EQUAL_INT(-1, vision::bestIndex(f));
}

void test_count_and_best_index_for_points(void) {
    vision::Frame f;
    f.kind = vision::Kind::Points;
    f.points = {{5, 6, 0, 40, 0}, {7, 8, 0, 90, 1}};
    TEST_ASSERT_EQUAL_INT(2, f.count());
    TEST_ASSERT_EQUAL_INT(1, vision::bestIndex(f));
}

void test_best_index_tie_first_wins(void) {
    vision::Frame f;
    f.kind = vision::Kind::Boxes;
    f.boxes = {{0, 0, 1, 1, 80, 0}, {0, 0, 1, 1, 80, 1}};
    TEST_ASSERT_EQUAL_INT(0, vision::bestIndex(f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_classify_precedence);
    RUN_TEST(test_kind_name);
    RUN_TEST(test_frame_count_follows_kind);
    RUN_TEST(test_best_index_picks_highest_score);
    RUN_TEST(test_best_index_pose_uses_box_score);
    RUN_TEST(test_best_index_empty_is_minus_one);
    RUN_TEST(test_count_and_best_index_for_points);
    RUN_TEST(test_best_index_tie_first_wins);
    UNITY_END();
    return 0;
}
