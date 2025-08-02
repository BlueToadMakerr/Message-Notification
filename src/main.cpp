#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>
#include <cocos2d.h>

#include <chrono>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

class MessageCheckerLayer : public cocos2d::CCLayer {
protected:
    std::chrono::steady_clock::time_point m_nextCheck;

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
        std::vector<std::string> parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") {
                ++i;
                continue;
            }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
            "GJ_messageIcon_001.png",
            false
        );
        log::info("notified user with title: {}", title);
    }

    void checkMessages() {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("not logged in, skipping check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("sending HTTP request with data: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageCheckerLayer::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::info("network request failed");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (raw.empty() || raw == "-1") return;

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) {
            currentClean.push_back(cleanMessage(msg));
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean) {
            if (std::find(old.begin(), old.end(), clean) == old.end()) {
                newMessages.push_back(clean);
            }
        }

        if (!newMessages.empty()) {
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() > 9) {
                    std::string sender = parts[1];
                    std::string subjectBase64 = parts[9];
                    std::string subject;
                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                    } else {
                        subject = "<Invalid base64>";
                    }
                    showNotification(fmt::format("New Message!\nSent by: {}\n{}", sender, subject));
                } else {
                    showNotification("New Message!");
                }
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        }

        // Save new messages
        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
    }

    void tick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
        } catch (...) {}

        auto now = std::chrono::steady_clock::now();
        if (now >= m_nextCheck) {
            checkMessages();
            m_nextCheck = now + std::chrono::seconds(interval);
        }
    }

public:
    static MessageCheckerLayer* create() {
        auto ret = new MessageCheckerLayer();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        m_nextCheck = std::chrono::steady_clock::now();
        this->schedule(schedule_selector(MessageCheckerLayer::tick), 1.0f);
        return true;
    }
};

// Attach the layer once at game boot
class $modify(MenuLayerHook, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        if (!CCDirector::sharedDirector()->getRunningScene()->getChildByID("MessageCheckerLayer")) {
            auto layer = MessageCheckerLayer::create();
            layer->setID("MessageCheckerLayer");
            CCDirector::sharedDirector()->getRunningScene()->addChild(layer, 9999);
            log::info("MessageCheckerLayer added to scene.");
        }

        return true;
    }
};