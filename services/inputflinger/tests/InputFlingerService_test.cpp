/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <BnInputFlingerQuery.h>
#include <IInputFlingerQuery.h>

#include <android/os/BnInputFlinger.h>
#include <android/os/BnSetInputWindowsListener.h>
#include <android/os/IInputFlinger.h>
#include <android/os/ISetInputWindowsListener.h>

#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>

#include <input/Input.h>
#include <input/InputTransport.h>
#include <input/InputWindow.h>

#include <gtest/gtest.h>
#include <inttypes.h>
#include <linux/uinput.h>
#include <log/log.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <chrono>
#include <thread>
#include <unordered_map>

#define TAG "InputFlingerServiceTest"

using android::os::BnInputFlinger;
using android::os::BnSetInputWindowsListener;
using android::os::IInputFlinger;
using android::os::ISetInputWindowsListener;

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

namespace android {

static const sp<IBinder> TestInfoToken = new BBinder();
static constexpr int32_t TestInfoId = 1;
static const std::string TestInfoName = "InputFlingerServiceTestInputWindowInfo";
static constexpr int32_t TestInfoLayoutParamsFlags = 0xABCD;
static constexpr int32_t TestInfoLayoutParamsType = 39;
static constexpr std::chrono::duration TestInfoDispatchingTimeout = 2532ms;
static constexpr int32_t TestInfoFrameLeft = 93;
static constexpr int32_t TestInfoFrameTop = 34;
static constexpr int32_t TestInfoFrameRight = 16;
static constexpr int32_t TestInfoFrameBottom = 19;
static constexpr int32_t TestInfoSurfaceInset = 17;
static constexpr float TestInfoGlobalScaleFactor = 0.3;
static constexpr float TestInfoWindowXScale = 0.4;
static constexpr float TestInfoWindowYScale = 0.5;
static const Rect TestInfoTouchableRegionRect = {100 /* left */, 150 /* top */, 400 /* right */,
                                                 450 /* bottom */};
static const Region TestInfoTouchableRegion(TestInfoTouchableRegionRect);
static constexpr bool TestInfoVisible = false;
static constexpr bool TestInfoCanReceiveKeys = false;
static constexpr bool TestInfoTrustedOverlay = true;
static constexpr bool TestInfoHasFocus = false;
static constexpr bool TestInfoHasWallpaper = false;
static constexpr bool TestInfoPaused = false;
static constexpr int32_t TestInfoOwnerPid = 19;
static constexpr int32_t TestInfoOwnerUid = 24;
static constexpr int32_t TestInfoInputFeatures = 29;
static constexpr int32_t TestInfoDisplayId = 34;
static constexpr int32_t TestInfoPortalToDisplayId = 2;
static constexpr bool TestInfoReplaceTouchableRegionWithCrop = true;
static const sp<IBinder> TestInfoTouchableRegionCropHandle = new BBinder();

static const std::string TestAppInfoName = "InputFlingerServiceTestInputApplicationInfo";
static const sp<IBinder> TestAppInfoToken = new BBinder();
static constexpr std::chrono::duration TestAppInfoDispatchingTimeout = 12345678ms;

static const String16 kTestServiceName = String16("InputFlingerService");
static const String16 kQueryServiceName = String16("InputFlingerQueryService");

struct SetInputWindowsListener;
// --- InputFlingerServiceTest ---
class InputFlingerServiceTest : public testing::Test {
public:
    void SetUp() override;
    void TearDown() override;

protected:
    void InitializeInputFlinger();
    void setInputWindowsByInfos(std::vector<InputWindowInfo>& infos);

    void setInputWindowsFinished();
    void verifyInputWindowInfo(const InputWindowInfo& info) const;
    InputWindowInfo& getInfo() const { return const_cast<InputWindowInfo&>(mInfo); }

    sp<IInputFlinger> mService;
    sp<IInputFlingerQuery> mQuery;

private:
    sp<SetInputWindowsListener> mSetInputWindowsListener;
    sp<InputChannel> mServerChannel, mClientChannel;
    InputWindowInfo mInfo;
    std::mutex mLock;
    std::condition_variable mSetInputWindowsFinishedCondition;
};

struct SetInputWindowsListener : BnSetInputWindowsListener {
    explicit SetInputWindowsListener(std::function<void()> cbFunc) : mCbFunc(cbFunc) {}

    binder::Status onSetInputWindowsFinished() override;

    std::function<void()> mCbFunc;
};

class TestInputManager : public BnInputFlinger {
protected:
    virtual ~TestInputManager(){};

public:
    TestInputManager(){};
    void checkFdFlags(const android::base::unique_fd& fd);

    binder::Status getInputWindows(std::vector<::android::InputWindowInfo>* inputHandles);
    binder::Status getInputChannels(std::vector<::android::InputChannelInfo>* infos);

    status_t dump(int fd, const Vector<String16>& args) override;

    binder::Status setInputWindows(
            const std::vector<InputWindowInfo>& handles,
            const sp<ISetInputWindowsListener>& setInputWindowsListener) override;

    binder::Status registerInputChannel(const InputChannelInfo& channel) override;
    binder::Status unregisterInputChannel(const InputChannelInfo& channel) override;

private:
    mutable Mutex mLock;
    std::unordered_map<int32_t, std::vector<sp<InputWindowHandle>>> mHandlesPerDisplay;
    std::vector<sp<InputChannel>> mInputChannels;
};

class TestInputQuery : public BnInputFlingerQuery {
public:
    TestInputQuery(sp<android::TestInputManager> manager) : mManager(manager){};
    binder::Status getInputWindows(std::vector<::android::InputWindowInfo>* inputHandles) override;
    binder::Status getInputChannels(std::vector<::android::InputChannelInfo>* infos) override;

private:
    sp<android::TestInputManager> mManager;
};

binder::Status TestInputQuery::getInputWindows(
        std::vector<::android::InputWindowInfo>* inputHandles) {
    return mManager->getInputWindows(inputHandles);
}

binder::Status TestInputQuery::getInputChannels(std::vector<::android::InputChannelInfo>* infos) {
    return mManager->getInputChannels(infos);
}

binder::Status SetInputWindowsListener::onSetInputWindowsFinished() {
    if (mCbFunc != nullptr) {
        mCbFunc();
    }
    return binder::Status::ok();
}

binder::Status TestInputManager::setInputWindows(
        const std::vector<InputWindowInfo>& infos,
        const sp<ISetInputWindowsListener>& setInputWindowsListener) {
    AutoMutex _l(mLock);

    for (const auto& info : infos) {
        mHandlesPerDisplay.emplace(info.displayId, std::vector<sp<InputWindowHandle>>());
        mHandlesPerDisplay[info.displayId].push_back(new InputWindowHandle(info));
    }
    if (setInputWindowsListener) {
        setInputWindowsListener->onSetInputWindowsFinished();
    }
    return binder::Status::ok();
}

void TestInputManager::checkFdFlags(const android::base::unique_fd& fd) {
    const int result = fcntl(fd, F_GETFL);
    EXPECT_NE(result, -1);
    EXPECT_EQ(result & O_NONBLOCK, O_NONBLOCK);
}

binder::Status TestInputManager::registerInputChannel(const InputChannelInfo& info) {
    AutoMutex _l(mLock);
    // check Fd flags
    checkFdFlags(info.mFd);

    android::base::unique_fd newFd(::dup(info.mFd));
    sp<InputChannel> channel = InputChannel::create(info.mName, std::move(newFd), info.mToken);
    mInputChannels.push_back(channel);

    return binder::Status::ok();
}

binder::Status TestInputManager::unregisterInputChannel(const InputChannelInfo& info) {
    AutoMutex _l(mLock);
    // check Fd flags
    checkFdFlags(info.mFd);
    android::base::unique_fd newFd(::dup(info.mFd));
    sp<InputChannel> channel = InputChannel::create(info.mName, std::move(newFd), info.mToken);

    auto it = std::find_if(mInputChannels.begin(), mInputChannels.end(),
                           [&](sp<InputChannel>& it) { return *it == *channel; });
    if (it != mInputChannels.end()) {
        mInputChannels.erase(it);
    }

    return binder::Status::ok();
}

status_t TestInputManager::dump(int fd, const Vector<String16>& args) {
    std::string dump;

    dump += " InputFlinger dump\n";

    ::write(fd, dump.c_str(), dump.size());
    return NO_ERROR;
}

binder::Status TestInputManager::getInputWindows(
        std::vector<::android::InputWindowInfo>* inputInfos) {
    for (auto& [displayId, inputHandles] : mHandlesPerDisplay) {
        for (auto& inputHandle : inputHandles) {
            inputInfos->push_back(*inputHandle->getInfo());
        }
    }
    return binder::Status::ok();
}

binder::Status TestInputManager::getInputChannels(std::vector<::android::InputChannelInfo>* infos) {
    infos->clear();
    for (auto& channel : mInputChannels) {
        auto chanDup = channel->dup();
        infos->push_back(std::move(chanDup->getInfo()));
    }
    return binder::Status::ok();
}

void InputFlingerServiceTest::SetUp() {
    mSetInputWindowsListener = new SetInputWindowsListener([&]() {
        std::unique_lock<std::mutex> lock(mLock);
        mSetInputWindowsFinishedCondition.notify_all();
    });
    InputChannel::openInputChannelPair("testchannels", mServerChannel, mClientChannel);

    mInfo.token = TestInfoToken;
    mInfo.id = TestInfoId;
    mInfo.name = TestInfoName;
    mInfo.layoutParamsFlags = TestInfoLayoutParamsFlags;
    mInfo.layoutParamsType = TestInfoLayoutParamsType;
    mInfo.dispatchingTimeout = TestInfoDispatchingTimeout;
    mInfo.frameLeft = TestInfoFrameLeft;
    mInfo.frameTop = TestInfoFrameTop;
    mInfo.frameRight = TestInfoFrameRight;
    mInfo.frameBottom = TestInfoFrameBottom;
    mInfo.surfaceInset = TestInfoSurfaceInset;
    mInfo.globalScaleFactor = TestInfoGlobalScaleFactor;
    mInfo.windowXScale = TestInfoWindowXScale;
    mInfo.windowYScale = TestInfoWindowYScale;
    mInfo.touchableRegion = TestInfoTouchableRegion;
    mInfo.visible = TestInfoVisible;
    mInfo.canReceiveKeys = TestInfoCanReceiveKeys;
    mInfo.trustedOverlay = TestInfoTrustedOverlay;
    mInfo.hasFocus = TestInfoHasFocus;
    mInfo.hasWallpaper = TestInfoHasWallpaper;
    mInfo.paused = TestInfoPaused;
    mInfo.ownerPid = TestInfoOwnerPid;
    mInfo.ownerUid = TestInfoOwnerUid;
    mInfo.inputFeatures = TestInfoInputFeatures;
    mInfo.displayId = TestInfoDisplayId;
    mInfo.portalToDisplayId = TestInfoPortalToDisplayId;
    mInfo.replaceTouchableRegionWithCrop = TestInfoReplaceTouchableRegionWithCrop;
    mInfo.touchableRegionCropHandle = TestInfoTouchableRegionCropHandle;

    mInfo.applicationInfo.name = TestAppInfoName;
    mInfo.applicationInfo.token = TestAppInfoToken;
    mInfo.applicationInfo.dispatchingTimeout = TestAppInfoDispatchingTimeout;

    InitializeInputFlinger();
}

void InputFlingerServiceTest::TearDown() {}

void InputFlingerServiceTest::verifyInputWindowInfo(const InputWindowInfo& info) const {
    EXPECT_EQ(mInfo, info);
}

void InputFlingerServiceTest::InitializeInputFlinger() {
    sp<IBinder> input(defaultServiceManager()->waitForService(kTestServiceName));
    ASSERT_TRUE(input != nullptr);
    mService = interface_cast<IInputFlinger>(input);

    input = defaultServiceManager()->waitForService(kQueryServiceName);
    ASSERT_TRUE(input != nullptr);
    mQuery = interface_cast<IInputFlingerQuery>(input);
}

void InputFlingerServiceTest::setInputWindowsByInfos(std::vector<InputWindowInfo>& infos) {
    std::unique_lock<std::mutex> lock(mLock);
    mService->setInputWindows(infos, mSetInputWindowsListener);
    // Verify listener call
    EXPECT_NE(mSetInputWindowsFinishedCondition.wait_for(lock, 1s), std::cv_status::timeout);
    // Verify input windows from service
    std::vector<::android::InputWindowInfo> inputHandles;
    mQuery->getInputWindows(&inputHandles);
    for (auto& inputInfo : inputHandles) {
        verifyInputWindowInfo(inputInfo);
    }
}

/**
 *  Test InputFlinger service interface SetInputWindows
 */
TEST_F(InputFlingerServiceTest, InputWindow_SetInputWindows) {
    std::vector<InputWindowInfo> infos = {getInfo()};
    setInputWindowsByInfos(infos);
}

/**
 *  Test InputFlinger service interface registerInputChannel
 */
TEST_F(InputFlingerServiceTest, InputWindow_RegisterInputChannel) {
    sp<InputChannel> serverChannel, clientChannel;

    InputChannel::openInputChannelPair("testchannels", serverChannel, clientChannel);
    mService->registerInputChannel(serverChannel->getInfo());

    std::vector<::android::InputChannelInfo> infos(2);
    mQuery->getInputChannels(&infos);
    EXPECT_EQ(infos.size(), 1UL);

    auto& info = infos[0];
    android::base::unique_fd newFd(::dup(info.mFd));
    sp<InputChannel> channel = InputChannel::create(info.mName, std::move(newFd), info.mToken);
    EXPECT_EQ(*channel, *serverChannel);

    mService->unregisterInputChannel(serverChannel->getInfo());
    mQuery->getInputChannels(&infos);
    EXPECT_EQ(infos.size(), 0UL);
}

/**
 *  Test InputFlinger service interface registerInputChannel with invalid cases
 */
TEST_F(InputFlingerServiceTest, InputWindow_RegisterInputChannelInvalid) {
    sp<InputChannel> serverChannel, clientChannel;
    InputChannel::openInputChannelPair("testchannels", serverChannel, clientChannel);

    std::vector<::android::InputChannelInfo> infos(2);
    mQuery->getInputChannels(&infos);
    EXPECT_EQ(infos.size(), 0UL);

    mService->registerInputChannel(InputChannelInfo());
    mService->unregisterInputChannel(clientChannel->getInfo());

    mService->registerInputChannel(serverChannel->getInfo());
    mService->registerInputChannel(clientChannel->getInfo());
    mQuery->getInputChannels(&infos);
    EXPECT_EQ(infos.size(), 2UL);

    mService->unregisterInputChannel(clientChannel->getInfo());
    mService->unregisterInputChannel(serverChannel->getInfo());
    mQuery->getInputChannels(&infos);
    EXPECT_EQ(infos.size(), 0UL);
}

} // namespace android

int main(int argc, char** argv) {
    pid_t forkPid = fork();

    if (forkPid == 0) {
        // Server process
        android::sp<android::TestInputManager> manager = new android::TestInputManager();
        android::sp<android::TestInputQuery> query = new android::TestInputQuery(manager);

        android::defaultServiceManager()->addService(android::kTestServiceName, manager,
                                                     false /*allowIsolated*/);
        android::defaultServiceManager()->addService(android::kQueryServiceName, query,
                                                     false /*allowIsolated*/);
        android::ProcessState::self()->startThreadPool();
        android::IPCThreadState::self()->joinThreadPool();
    } else {
        android::ProcessState::self()->startThreadPool();
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }
    return 0;
}