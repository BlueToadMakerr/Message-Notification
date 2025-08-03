#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

class MessageChecker : public cocos2d::CCNode {
public:
    static MessageChecker* getInstance() {
        static MessageChecker instance;
        return &instance;
    }

    void start() {
        if (!_started) {
            _started = true;
            this->schedule(schedule_selector(MessageChecker::onScheduledTick), 1.0f);
            nextCheck = std::chrono::steady_clock::now();
            log::info("MessageChecker started");
        }
    }

private:
    bool _started = false;
    std::chrono::steady_clock::time_point nextCheck;

    std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    std::string cleanMessage(const std::string& msg) {
        auto parts = split(msg, ':');
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
            title.c_str(), "", "GJ_messageIcon_001.png", false
        );
        log::info("notified user with title: {}", title);
    }

    void checkMessages() {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("not logged in");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("sending HTTP request: {}", postData);

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
        if (!resp || !resp->isSucceed()) {
            log::info("network error");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("server response: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("no new messages");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) {
            auto cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("cleaned: {}", cleaned);
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (const auto& clean : currentClean) {
            if (std::find(old.begin(), old.end(), clean) == old.end()) {
                newMessages.push_back(clean);
            }
        }

        if (!newMessages.empty()) {
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
                    showNotification(fmt::format("New Message!\nSent by: {}\n{}", sender, subject));
                } else {
                    showNotification("New Message!");
                }
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
        } catch (...) {
            // default
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            log::info("interval elapsed, checking messages");
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(nextCheck - now).count();
            log::info("waiting {}s before next check", remain);
        }
    }
};

// Hook MenuLayer to start MessageChecker
class $modify(MenuLayerHook, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        MessageChecker::getInstance()->start();
        return true;
    }
};

// Hook PlayLayer to start MessageChecker
class $modify(PlayLayerHook, PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level, false, false)) return false;
        MessageChecker::getInstance()->start();
        return true;
    }
};
