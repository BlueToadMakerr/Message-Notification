#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>

using namespace geode::prelude;

class MessageCheckerNode : public cocos2d::CCNode {
protected:
    std::chrono::steady_clock::time_point nextCheck;

    bool init() override {
        log::info("[MessageCheckerNode] Initializing and scheduling tick.");
        nextCheck = std::chrono::steady_clock::now();
        this->schedule(schedule_selector(MessageCheckerNode::onTick), 1.0f);
        return true;
    }

    void onTick(float) {
        log::info("[MessageCheckerNode] Tick triggered.");

        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
            log::info("[MessageCheckerNode] Interval from settings: {} seconds", interval);
        } catch (...) {
            log::info("[MessageCheckerNode] Failed to get interval from settings, using default 300s.");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            log::info("[MessageCheckerNode] Time to check messages.");
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(nextCheck - now).count();
            log::info("[MessageCheckerNode] Next check in {} seconds.", remain);
        }
    }

    void checkMessages() {
        log::info("[MessageCheckerNode] Performing message check.");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[MessageCheckerNode] Not logged in, skipping message check.");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("[MessageCheckerNode] Sending request with data: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageCheckerNode::onResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::stringstream ss(str);
        std::string item;
        std::vector<std::string> elems;
        while (std::getline(ss, item, delim)) elems.push_back(item);
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
        std::vector<std::string> parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") { ++i; continue; }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void onResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        log::info("[MessageCheckerNode] Received HTTP response.");
        if (!resp || !resp->isSucceed()) {
            log::info("[MessageCheckerNode] Request failed or null response.");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("[MessageCheckerNode] Raw response: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("[MessageCheckerNode] No messages found.");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (auto& msg : currentRaw) {
            auto cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("[MessageCheckerNode] Cleaned message: {}", cleaned);
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean)
            if (std::find(old.begin(), old.end(), clean) == old.end()) {
                newMessages.push_back(clean);
                log::info("[MessageCheckerNode] New message found: {}", clean);
            }

        if (!newMessages.empty()) {
            log::info("[MessageCheckerNode] Found {} new message(s).", newMessages.size());

            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() > 9) {
                    std::string sender = parts[1];
                    std::string subject = "<Invalid base64>";
                    auto decoded = geode::utils::base64::decode(parts[9]);
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                    }
                    std::string msg = fmt::format("New Message!\nFrom: {}\n{}", sender, subject);
                    notify(msg);
                } else notify("New Message!");
            } else notify(fmt::format("{} New Messages!", newMessages.size()));
        } else {
            log::info("[MessageCheckerNode] No new messages since last check.");
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
        log::info("[MessageCheckerNode] Saved current message list.");
    }

    void notify(const std::string& msg) {
        log::info("[MessageCheckerNode] Notifying user: {}", msg);
        AchievementNotifier::sharedState()->notifyAchievement(
            msg.c_str(), "", "GJ_messageIcon_001.png", false
        );
    }

public:
    static MessageCheckerNode* create() {
        auto ret = new MessageCheckerNode();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

static MessageCheckerNode* g_checker = nullptr;

// Reattach globally across all scenes
class $modify(SceneMessageAttacher, cocos2d::CCScene) {
    void onEnter() {
        CCScene::onEnter();
        log::info("[SceneMessageAttacher] Scene entered.");

        if (!g_checker) {
            log::info("[SceneMessageAttacher] Creating MessageCheckerNode.");
            g_checker = MessageCheckerNode::create();
            g_checker->retain();
        }

        if (g_checker->getParent()) {
            log::info("[SceneMessageAttacher] Removing old parent.");
            g_checker->removeFromParent();
        }

        log::info("[SceneMessageAttacher] Adding checker to new scene.");
        this->addChild(g_checker);
    }
};
