#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>

using namespace geode::prelude;

class MessageChecker : public cocos2d::CCNode {
public:
    static MessageChecker* create() {
        auto ret = new MessageChecker();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        this->schedule(schedule_selector(MessageChecker::onScheduledTick), 1.0f);
        onScheduledTick(0);
        return true;
    }

private:
    std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();

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
                ++i; // skip tag value
                continue;
            }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(), "", "GJ_messageIcon_001.png", false
        );
        log::info("notified user with title: {}", title);
    }

    void checkMessages() {
        log::info("checking for new messages");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("not logged in, skipping check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");
        req->setResponseCallback(this, SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) return;

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
                std::string subject = "<Invalid base64>";
                if (parts.size() > 9) {
                    auto decoded = geode::utils::base64::decode(parts[9]);
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                    }
                }
                std::string notif = fmt::format("New Message!\nSent by: {}\n{}", parts[1], subject);
                showNotification(notif);
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
    }

    void onScheduledTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
        } catch (...) {}

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        }
    }
};

// Inject MessageChecker into every new scene
class $modify(SceneChecker, cocos2d::CCScene) {
    void onEnter() {
        CCScene::onEnter();

        if (!this->getChildByID("message-checker"_spr)) {
            log::info("Adding MessageChecker to scene: {}", typeid(*this).name());
            auto checker = MessageChecker::create();
            checker->setID("message-checker"_spr);
            this->addChild(checker, 9999);
        }
    }
};