

#include "proto/libflame.grpc.pb.h"
#include "proto/libflame.pb.h"
#include "service/log_service.h"
#include "libflame/libchunk/libchunk.h"

#include <grpcpp/grpcpp.h>
#include <regex>
#include <string>
#include <gtest/gtest.h>
#include <iostream>

#include "CxxTestDefs.h"
#include "include/libflame.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using namespace std;

#ifdef GTEST
#define private public
#define protected public
#endif

namespace flame {
class TestEnvironment : public testing::Environment{
public:
    virtual void SetUp()
    {
        cout << "TestEnvironment SetUp" << endl;
    }
    virtual void TearDown()
    {
        cout << "TestEnvironment TearDown" << endl;
    }
};//class TestEnvironment

//在第一个test之前，最后一个test之后调用SetUpTestCase()和TearDownTestCase()
class TestLibFlame:public testing::Test
{
public:
    TestLibFlame(){
        internal_ = 1;
        FlameContext* flame_context = FlameContext::get_context();
        uint64_t gw_id = 0;
    }
    ~TestLibFlame(){

    }

    void ChangeStudentAge(int n){
        cout << "Success" << endl;
    }
    void Print(){
        cout << "Success" << endl;
    }

    /*下述两个函数针对一个TestSuite*/
    static void SetUpTestCase()
    {
        cout<<"SetUpTestCase()"<<endl;
    }
 
    static void TearDownTestCase()
    {
        //delete s;
        cout<<"TearDownTestCase()"<<endl;
    }
    /* 下述两个函数针对每个TEST*/
    void SetUp()//构造后调用
    {
        cout<<"SetUp() is running"<<endl;
         
    }
    void TearDown()//析构前调用
    {
        cout<<"TearDown()"<<endl;
    }  
    
    FlameHandlers *flame_handlers_;
    int internal_;
};// class TestLibFlame

// class IsPrimeParamTest : public::testing::TestWithParam<int>{};

// TEST_P(IsPrimeParamTest, HandleTrueReturn)
// {
//     int n =  GetParam();
//     cout << n << endl;
// }
// INSTANTIATE_TEST_CASE_P(TrueReturn, IsPrimeParamTest, testing::Values(3,4,7));

}// libflame


