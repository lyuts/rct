#include "ProcessTestSuite.h"

#include <rct/Process.h>
#include <thread>
#include <mutex>
#include "UdpIncludes.h"

void ProcessTestSuite::setUp()
{
    int res;

#ifdef _WIN32
    WSADATA unused;
    CHECK_RETURN(WSAStartup(MAKEWORD(2,2), &unused) != 0, "WSAStartup()");
#endif

    listenSock = socket(AF_INET, SOCK_DGRAM, 0);
    sendSock = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK_RETURN(listenSock == INVALID_SOCKET || sendSock == INVALID_SOCKET, "socket()");

    const int optval = 1;
    res = setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (optval_p)&optval,
                     sizeof(optval));
    CHECK_RETURN(res != 0, "setsockopt(SO_REUSEADDR)");

    // set timeout on reads
#ifdef _WIN32
    timeout_t to = recvTimeout_ms;
#else
    timeout_t to;
    to.tv_sec = recvTimeout_ms / 1000;
    to.tv_usec = (recvTimeout_ms % 1000) * 1000;
#endif
    res = setsockopt(listenSock, SOL_SOCKET, SO_RCVTIMEO, (optval_p)&to,
                     sizeof(timeout_t));
    CHECK_RETURN(res != 0, "setsockopt(SO_RCVTIMEO)");

    sockaddr_in listenAddr;
    std::memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //only bind localhost
    listenAddr.sin_port = htons(1338);
    res = bind(listenSock, (sockaddr*)&listenAddr, sizeof(listenAddr));
    CHECK_RETURN(res < 0, "bind");
}

std::string ProcessTestSuite::udp_recv()
{
    static const size_t BUFFER_SIZE = 80;
    char buf[BUFFER_SIZE];
    ssize_t recvSize = recv(listenSock, buf, BUFFER_SIZE, 0);

    if(recvSize < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        std::cout << "Error reading from udp socket" << std::endl;
        exit(-1);
    }

    if(recvSize < 0) recvSize = 0;
    std::string ret(buf, recvSize);
    return ret;
}

void ProcessTestSuite::udp_send(const std::string &data)
{
    sockaddr_in sendAddr;
    std::memset(&sendAddr, 0, sizeof(sendAddr));
    sendAddr.sin_family = AF_INET;
    sendAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendAddr.sin_port = htons(1337);

    int res = sendto(sendSock, data.data(), data.size(), 0,
                     (sockaddr*)&sendAddr, sizeof(sendAddr));

    CHECK_RETURN((unsigned)res != data.size(), "Error sending UDP");
}

void ProcessTestSuite::tearDown()
{
    close(listenSock);
    close(sendSock);
}

void ProcessTestSuite::realSleep(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    timespec s;
    s.tv_sec = ms/1000;
    s.tv_nsec = (ms%1000) * 1000 * 1000;

    while(nanosleep(&s, &s) == -1 && errno == EINTR);
#endif
}

void ProcessTestSuite::returnCode()
{
    // tell child process to exit with code 12 in 50 ms from now
    std::thread t([this](){realSleep(50); udp_send("exit 12");});

    // start process
    Process p;
    p.exec("ChildProcess");  // synchronous call

    t.join();
    realSleep(50);

    // check exit code
    CPPUNIT_ASSERT(p.returnCode() == 12);
}

void ProcessTestSuite::startAsync()
{
    Process p;
    p.start("ChildProcess");  // asynchronous call
    CPPUNIT_ASSERT(!p.isFinished());
    realSleep(50);
    CPPUNIT_ASSERT(!p.isFinished());
    udp_send("exit 1");
    realSleep(50);
    CPPUNIT_ASSERT(p.isFinished());
    CPPUNIT_ASSERT(p.returnCode() == 1);
}

#ifndef _WIN32

void ProcessTestSuite::readFromStdout()
{
    Process p;
    std::vector<String> dataReadFromStdout;

    // execute ChildProcess synchronously, but in another thread, so
    // that we can monitor its stdout in the main thread.
    // Note: Calling ChildProcess asynchronously with Process::start()
    // and polling for stdout does not work because an asynchronous
    // Process requires a running EventLoop.
    std::thread t([&](){p.exec("ChildProcess");});

    realSleep(50);
    CPPUNIT_ASSERT(!p.isFinished());
    dataReadFromStdout.push_back(p.readAllStdOut());  // should be empty
    udp_send("stdout This is a test");
    realSleep(50);
    dataReadFromStdout.push_back(p.readAllStdOut());  // should be "This is a test"
    dataReadFromStdout.push_back(p.readAllStdOut());  // should be empty
    realSleep(50);
    dataReadFromStdout.push_back(p.readAllStdOut());  // should be empty
    udp_send("exit 0");
    realSleep(50);
    dataReadFromStdout.push_back(p.readAllStdOut());  // should be empty

    CPPUNIT_ASSERT(p.isFinished());

    // need to do the test *after* ChildProcess terminates, because
    // if the check fails, we exit this scope, destroying the Process object
    // before ChildProcess exits.

    CPPUNIT_ASSERT(dataReadFromStdout[0].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStdout[1] == "This is a test");
    CPPUNIT_ASSERT(dataReadFromStdout[2].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStdout[3].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStdout[4].isEmpty());
    t.join();
}

void ProcessTestSuite::readFromStderr()
{
    Process p;
    std::vector<String> dataReadFromStderr;

    // execute ChildProcess synchronously, but in another thread, so
    // that we can monitor its stdout in the main thread.
    // Note: Calling ChildProcess asynchronously with Process::start()
    // and polling for stdout does not work because an asynchronous
    // Process requires a running EventLoop.
    std::thread t([&](){p.exec("ChildProcess");});

    realSleep(50);
    CPPUNIT_ASSERT(!p.isFinished());
    dataReadFromStderr.push_back(p.readAllStdErr());  // should be empty
    udp_send("stderr This is a stderr test");
    realSleep(50);
    dataReadFromStderr.push_back(p.readAllStdErr());  // should be "This is a stderr test"
    dataReadFromStderr.push_back(p.readAllStdErr());  // should be empty
    realSleep(50);
    dataReadFromStderr.push_back(p.readAllStdErr());  // should be empty
    udp_send("exit 0");
    realSleep(50);
    dataReadFromStderr.push_back(p.readAllStdErr());  // should be empty

    CPPUNIT_ASSERT(p.isFinished());

    // need to do the test *after* ChildProcess terminates, because
    // if the check fails, we exit this scope, destroying the Process object
    // before ChildProcess exits.

    CPPUNIT_ASSERT(dataReadFromStderr[0].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStderr[1] == "This is a stderr test");
    CPPUNIT_ASSERT(dataReadFromStderr[2].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStderr[3].isEmpty());
    CPPUNIT_ASSERT(dataReadFromStderr[4].isEmpty());
    t.join();
}

void ProcessTestSuite::signals()
{
    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop);

    Process p;
    bool finishedCalled = false;
    bool wrongProcessObjPassed = false;
    std::string stdoutData, stderrData;

    p.readyReadStdOut().connect([&](Process* pp)
                                {
                                    if(pp != &p) wrongProcessObjPassed = true;
                                    stdoutData.append(pp->readAllStdOut().c_str());
                                });

    p.readyReadStdErr().connect([&](Process* pp)
                                {
                                    if(pp != &p) wrongProcessObjPassed = true;
                                    stderrData.append(pp->readAllStdErr().c_str());
                                });

    p.finished().connect([&](Process* pp)
                         {
                             if(pp != &p) wrongProcessObjPassed = true;
                             finishedCalled = true;
                         });

    p.start("ChildProcess");  // start asynchronously

    std::thread t([this]()
        {
            realSleep(50);
            udp_send("stdout Hello world");
            udp_send("stderr Error world");
            realSleep(50);
            udp_send("exit 0");
        });

    // Signals are deliviered by the running event loop.
    loop->exec(150);
    t.join();

    CPPUNIT_ASSERT(stdoutData == "Hello world");
    CPPUNIT_ASSERT(stderrData == "Error world");
    CPPUNIT_ASSERT(!wrongProcessObjPassed);
    CPPUNIT_ASSERT(finishedCalled);
}

void ProcessTestSuite::execTimeout()
{
    Process p;
    auto res = p.exec("ChildProcess", List<String>(), 200);  // timeout: 200 ms
    CPPUNIT_ASSERT(res == Process::TimedOut);
    CPPUNIT_ASSERT(p.isFinished());
    CPPUNIT_ASSERT(p.returnCode() == Process::ReturnKilled);
    CPPUNIT_ASSERT(p.errorString() == "Timed out");
}

void ProcessTestSuite::env()
{
    Process p;

    List<String> env;
    env.push_back("TESTVALUE=foo");

    std::thread t([&](){p.exec("ChildProcess", List<String>(), env);});

    realSleep(50);
    udp_send("getEnv");
    realSleep(50);
    String readEnv = p.readAllStdOut();
    udp_send("exit 0");

    t.join();

    //               0        1
    //               12345678901234 5 6 7
    String expected("TESTVALUE=foo\0\n\0\n", 17);

    CPPUNIT_ASSERT(readEnv == expected);
}

void ProcessTestSuite::writeToStdin()
{
    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop);

    Process p;
    std::string whatWeRead;

    p.start("ChildProcess");

    std::thread t([&]()
                  {
                      realSleep(50);
                      p.write("stdin write test");
                      realSleep(50);
                      whatWeRead = udp_recv();
                      udp_send("exit 0");
                      realSleep(50);
                  });

    loop->exec(200);

    t.join();

    CPPUNIT_ASSERT(p.isFinished());
    CPPUNIT_ASSERT(whatWeRead == "stdin write test");
}

#endif
