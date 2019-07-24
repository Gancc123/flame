#include "gtest/libflame/gtest_libflame.h"


using namespace flame;


TEST_F(TestLibFlame, TestVolume)
{
    VolumeMeta meta;
    meta.id = 1;
    meta.chk_sz = 1024;
    meta.chunks_map[0] = {123, 192, 7777};
    meta.chunks_map[1] = {124, 193, 7777};
    meta.chunks_map[2] = {125, 194, 7777};
    meta.chunks_map[3] = {126, 194, 7777};
    Volume* volume = new Volume(meta);
    std::vector<ChunkOffLen> chunk_positions;
    volume->_vol_to_chunks(512, 2048, chunk_positions);
    ASSERT_EQ(123,  chunk_positions[0].chunk_id);
    ASSERT_EQ(512,  chunk_positions[0].offset);
    ASSERT_EQ(512,  chunk_positions[0].length);
    ASSERT_EQ(124,  chunk_positions[1].chunk_id);
    ASSERT_EQ(0,    chunk_positions[1].offset);
    ASSERT_EQ(1024, chunk_positions[1].length);
    ASSERT_EQ(125,  chunk_positions[2].chunk_id);
    ASSERT_EQ(0,    chunk_positions[2].offset);
    ASSERT_EQ(512, chunk_positions[2].length);
    chunk_positions.clear();

    volume->_vol_to_chunks(1023, 2049, chunk_positions);
    ASSERT_EQ(123,  chunk_positions[0].chunk_id);
    ASSERT_EQ(1023,  chunk_positions[0].offset);
    ASSERT_EQ(1,  chunk_positions[0].length);
    ASSERT_EQ(124,  chunk_positions[1].chunk_id);
    ASSERT_EQ(0,    chunk_positions[1].offset);
    ASSERT_EQ(1024, chunk_positions[1].length);
    ASSERT_EQ(125,  chunk_positions[2].chunk_id);
    ASSERT_EQ(0,    chunk_positions[2].offset);
    ASSERT_EQ(1024, chunk_positions[2].length);
    chunk_positions.clear();

    volume->_vol_to_chunks(512, 2048, chunk_positions);
    ASSERT_EQ(123,  chunk_positions[0].chunk_id);
    ASSERT_EQ(512,  chunk_positions[0].offset);
    ASSERT_EQ(512,  chunk_positions[0].length);
    ASSERT_EQ(124,  chunk_positions[1].chunk_id);
    ASSERT_EQ(0,    chunk_positions[1].offset);
    ASSERT_EQ(1024, chunk_positions[1].length);
    chunk_positions.clear();

     VolumeMeta meta2;
    meta2.id = 1;
    meta2.chk_sz = 1073741824;
    meta2.chunks_map[0] = {123, 192, 7777};
    meta2.chunks_map[1] = {124, 193, 7777};
    meta2.chunks_map[2] = {125, 194, 7777};
    meta2.chunks_map[3] = {126, 194, 7777};
    Volume* volume2 = new Volume(meta2);
    uint64_t GigaByte = 1 << 30;
    volume2->_vol_to_chunks(GigaByte - 8192, 8192 * 2, chunk_positions);
    ASSERT_EQ(123,              chunk_positions[0].chunk_id);
    ASSERT_EQ(GigaByte - 8192,  chunk_positions[0].offset);
    ASSERT_EQ(8192,             chunk_positions[0].length);
    ASSERT_EQ(124,              chunk_positions[1].chunk_id);
    ASSERT_EQ(0,                chunk_positions[1].offset);
    ASSERT_EQ(8192,             chunk_positions[1].length);
    chunk_positions.clear();
    Print();
}
TEST_F(TestLibFlame, Test2)
{
    //ChangeStudentAge(55);
    // you can refer to s here
    Print();
}
TEST_F(TestLibFlame, Test3)
{
    Print();
    // you can refer to s here
    //print();
}

int main(int argc, char  **argv)
{
    testing::AddGlobalTestEnvironment(new TestEnvironment);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}