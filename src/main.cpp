#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GjGarageLayer.hpp>
#include <Geode/modify/GjShopLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelClearLayer.hpp>
#include <Geode/modify/LevelEditorPauseLayer.hpp>
#include <Geode/modify/LevelLeaderboardLayer.hpp>
#include <Geode/modify/LeaderboardLayer.hpp>
#include <Geode/modify/LevelsLayer.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/MapLayer.hpp>
#include <Geode/modify/MessageLayer.hpp>
#include <Geode/modify/NewgroundsLoginLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/PurchaseLayer.hpp>
#include <Geode/modify/SearchLayer.hpp>
#include <Geode/modify/SetLayer.hpp>
#include <Geode/modify/StatisticsLayer.hpp>
#include <Geode/modify/UserInfoLayer.hpp>

#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

class MessageCheckerBase : public cocos2d::Ref {
protected:
    std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();
    bool bootChecked = false;

public:
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
        auto parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") {
                ++i; // skip tag and its value
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

        req->setResponseCallback([this](cocos2d::extension::CCHttpClient* client, cocos2d::extension::CCHttpResponse* resp) {
            this->onMessageResponse(client, resp);
        });

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

                    std::string notif = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                    showNotification(notif);
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
        int interval = 300; // fallback default
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
            log::info("using check interval from settings: {}", interval);
        } catch (...) {
            log::info("failed to get check-interval setting, using default 300");
        }

        auto now = std::chrono::steady_clock::now();

        if (now >= nextCheck) {
            log::info("interval elapsed, performing message check");
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
            log::info("next check scheduled in {} seconds", interval);
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(nextCheck - now).count();
            log::info("waiting for next check: {} seconds left", remain);
        }
    }
};

static MessageCheckerBase g_messageChecker;

#define DEFINE_LAYER_CHECKER(LayerName) \
class $modify(MessageChecker_##LayerName, LayerName) { \
public: \
    bool init() override { \
        if (!LayerName::init()) return false; \
        log::info(#LayerName " init"); \
        if (!g_messageChecker.bootChecked) { \
            g_messageChecker.bootChecked = true; \
            log::info("boot sequence starting"); \
            this->schedule([&](float dt) { g_messageChecker.onScheduledTick(dt); }, 1.0f, "MessageCheckerScheduler"); \
            g_messageChecker.onScheduledTick(0); \
        } else { \
            log::info("boot already done, scheduling check"); \
            this->schedule([&](float dt) { g_messageChecker.onScheduledTick(dt); }, 1.0f, "MessageCheckerScheduler"); \
        } \
        return true; \
    } \
};

DEFINE_LAYER_CHECKER(MenuLayer)
DEFINE_LAYER_CHECKER(LevelSelectLayer)
DEFINE_LAYER_CHECKER(GjGarageLayer)
DEFINE_LAYER_CHECKER(GjShopLayer)
DEFINE_LAYER_CHECKER(CreatorLayer)
DEFINE_LAYER_CHECKER(LevelBrowserLayer)
DEFINE_LAYER_CHECKER(LevelClearLayer)
DEFINE_LAYER_CHECKER(LevelEditorPauseLayer)
DEFINE_LAYER_CHECKER(LevelLeaderboardLayer)
DEFINE_LAYER_CHECKER(LeaderboardLayer)
DEFINE_LAYER_CHECKER(LevelsLayer)
DEFINE_LAYER_CHECKER(LoadingLayer)
DEFINE_LAYER_CHECKER(MapLayer)
DEFINE_LAYER_CHECKER(MessageLayer)
DEFINE_LAYER_CHECKER(NewgroundsLoginLayer)
DEFINE_LAYER_CHECKER(PlayLayer)
DEFINE_LAYER_CHECKER(ProfilePage)
DEFINE_LAYER_CHECKER(PurchaseLayer)
DEFINE_LAYER_CHECKER(SearchLayer)
DEFINE_LAYER_CHECKER(SetLayer)
DEFINE_LAYER_CHECKER(StatisticsLayer)
DEFINE_LAYER_CHECKER(UserInfoLayer)

