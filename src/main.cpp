#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>

using namespace geode::prelude;

class MessageTicker : public CCNode {
public:
    static MessageTicker* get() {
        static auto* instance = MessageTicker::create();
        return instance;
    }

    static MessageTicker* create() {
        auto ret = new MessageTicker();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        this->schedule(schedule_selector(MessageTicker::onTick), 1.0f);
        log::info("[Message Notifications]: Ticker started");
        return true;
    }

private:
    std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();

    void onTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
        } catch (...) {
            log::info("[Message Notifications]: failed to get check-interval setting, using default 300");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            log::info("[Message Notifications]: ✅ Interval reached! Checking for new messages...");
            nextCheck = now + std::chrono::seconds(interval);
            checkMessages();
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(nextCheck - now).count();
            auto eta = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + std::chrono::seconds(remain));
            log::info("[Message Notifications]: ⏳ {}s remaining until next message check (ETA: {})", remain, std::ctime(&eta));
        }
    }

    void checkMessages() {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[Message Notifications]: Not logged in, skipping message check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageTicker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::info("[Message Notifications]: HTTP failed or no response");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("[Message Notifications]: Server response: {}", raw);

        if (raw.empty() || raw == "-1") return;

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw)
            currentClean.push_back(cleanMessage(msg));

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

                    std::string notif = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                    showNotification(notif);
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

    static void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), "", "GJ_messageIcon_001.png", false);
    }

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim))
            elems.push_back(item);
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
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
};

// Inject ticker into every scene once
class $modify(MessageTickerSceneHook, CCScene) {
    void onEnter() {
        CCScene::onEnter();

        if (!MessageTicker::get()->getParent()) {
            this->addChild(MessageTicker::get(), INT_MAX);
            log::info("[Message Notifications]: Injected ticker into scene");
        }
    }
};
