#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/GJShopLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/SecretRewardsLayer.hpp>
#include <Geode/modify/SecretLayer.hpp>
#include <Geode/modify/SecretLayer2.hpp>
#include <Geode/modify/SecretLayer3.hpp>
#include <Geode/modify/SecretLayer4.hpp>
#include <Geode/modify/SecretLayer5.hpp>
#include <Geode/modify/LevelAreaInnerLayer.hpp>
#include <Geode/modify/GauntletSelectLayer.hpp>
#include <Geode/modify/GauntletLayer.hpp>

#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>

using namespace geode::prelude;

#define CHECKER_INIT(LayerName) \
    bool init() { \
        if (!LayerName::init()) return false; \
        log::info("{} init", #LayerName); \
        if (!Fields::bootChecked) { \
            Fields::bootChecked = true; \
            log::info("boot sequence starting"); \
            this->schedule(schedule_selector(MessageChecker::onScheduledTick), 1.0f); \
            onScheduledTick(0); \
        } else { \
            log::info("boot already done, skipping init check and re-running the schedule"); \
            this->schedule(schedule_selector(MessageChecker::onScheduledTick), 1.0f); \
        } \
        return true; \
    }

class MessageCheckerBase {
protected:
    struct Fields {
        inline static std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();
        inline static bool bootChecked = false;
    };

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
        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), "", "GJ_messageIcon_001.png", false);
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
        log::info("sending HTTP request with data: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageCheckerBase::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        log::info("received response from server");
        if (!resp || !resp->isSucceed()) {
            log::info("network request failed or was null");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("raw server response: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("no messages found in server response");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) {
            std::string cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("cleaned message: {}", cleaned);
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean) {
            if (std::find(old.begin(), old.end(), clean) == old.end()) {
                newMessages.push_back(clean);
                log::info("new message detected: {}", clean);
            }
        }

        if (!newMessages.empty()) {
            log::info("total new messages: {}", newMessages.size());
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() > 9) {
                    std::string sender = parts[1];
                    std::string subjectBase64 = parts[9];
                    log::info("base64 subject: {}", subjectBase64);
                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    std::string subject;
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                        log::info("decoded subject: {}", subject);
                    } else {
                        subject = "<Invalid base64>";
                        log::info("failed to decode base64 subject");
                    }
                    showNotification(fmt::format("New Message!\nSent by: {}\n{}", sender, subject));
                } else {
                    showNotification("New Message!");
                }
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        } else {
            log::info("no new messages compared to saved list");
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
        log::info("saved current message state to settings");
    }

    void onScheduledTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
            log::info("using check interval from settings: {}", interval);
        } catch (...) {
            log::info("failed to get check-interval setting, using default 300");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= Fields::nextCheck) {
            log::info("interval elapsed, performing message check");
            checkMessages();
            Fields::nextCheck = now + std::chrono::seconds(interval);
            log::info("next check scheduled in {} seconds", interval);
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(Fields::nextCheck - now).count();
            log::info("waiting for next check: {} seconds left", remain);
        }
    }
};

#define MAKE_MESSAGE_CHECKER(LayerName) \
class $modify(MessageChecker_##LayerName, LayerName, MessageCheckerBase) { \
    CHECKER_INIT(LayerName) \
};

MAKE_MESSAGE_CHECKER(MenuLayer)
MAKE_MESSAGE_CHECKER(LevelSelectLayer)
MAKE_MESSAGE_CHECKER(GJGarageLayer)
MAKE_MESSAGE_CHECKER(GJShopLayer)
MAKE_MESSAGE_CHECKER(CreatorLayer)
MAKE_MESSAGE_CHECKER(LevelBrowserLayer)
MAKE_MESSAGE_CHECKER(LevelInfoLayer)
MAKE_MESSAGE_CHECKER(LevelEditorLayer)
MAKE_MESSAGE_CHECKER(PlayLayer)
MAKE_MESSAGE_CHECKER(SecretRewardsLayer)
MAKE_MESSAGE_CHECKER(SecretLayer)
MAKE_MESSAGE_CHECKER(SecretLayer2)
MAKE_MESSAGE_CHECKER(SecretLayer3)
MAKE_MESSAGE_CHECKER(SecretLayer4)
MAKE_MESSAGE_CHECKER(SecretLayer5)
MAKE_MESSAGE_CHECKER(LevelAreaInnerLayer)
MAKE_MESSAGE_CHECKER(GauntletSelectLayer)
MAKE_MESSAGE_CHECKER(GauntletLayer)
