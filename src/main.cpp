#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>

using namespace geode::prelude;

struct MessageCheckerState {
    inline static std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();
    inline static bool bootChecked = false;
};

std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> elems;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) elems.push_back(item);
    return elems;
}

std::string cleanMessage(const std::string& msg) {
    auto parts = split(msg, ':');
    std::string cleaned;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == "7" || parts[i] == "8") {
            ++i; // skip value after 7 or 8
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

void checkMessages();

void onScheduledTick(float);

/////////////////////////
// MessageChecker base  //
/////////////////////////

class MessageCheckerBase : public cocos2d::CCObject {
public:
    // This handles the HTTP response callback
    void onMessageResponse(cocos2d::CCNode* sender, void* data) {
        auto* response = static_cast<cocos2d::extension::CCHttpResponse*>(data);
        if (!response || !response->isSucceed()) {
            log::warn("HTTP request failed: {}", response ? response->getErrorBuffer() : "null response");
            return;
        }

        std::vector<char>* buffer = response->getResponseData();
        std::string raw(buffer->begin(), buffer->end());
        log::info("raw server response: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("no messages found in server response");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) {
            auto cleaned = cleanMessage(msg);
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
                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    std::string subject;
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
        req->setResponseCallback(this, callfuncND_selector(MessageCheckerBase::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }
};

void onScheduledTick(float) {
    int interval = 300;
    try {
        interval = std::clamp(Mod::get()->getSettingValue<int>("check-interval"), 60, 600);
        log::info("using check interval from settings: {}", interval);
    } catch (...) {
        log::info("failed to get check-interval setting, using default 300");
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= MessageCheckerState::nextCheck) {
        MessageCheckerBase checker;
        checker.checkMessages();
        MessageCheckerState::nextCheck = now + std::chrono::seconds(interval);
        log::info("next check scheduled in {} seconds", interval);
    } else {
        auto remain = std::chrono::duration_cast<std::chrono::seconds>(MessageCheckerState::nextCheck - now).count();
        log::info("waiting for next check: {} seconds left", remain);
    }
}

//////////////////////////
// Modify MenuLayer      //
//////////////////////////

class $modify(MessageCheckerMenu, MenuLayer), public MessageCheckerBase {
public:
    bool init() override {
        if (!MenuLayer::init()) return false;
        log::info("MenuLayer init");

        if (!MessageCheckerState::bootChecked) {
            MessageCheckerState::bootChecked = true;
            this->schedule(schedule_selector(MessageCheckerMenu::onTick), 1.0f);
            onScheduledTick(0);
        } else {
            this->schedule(schedule_selector(MessageCheckerMenu::onTick), 1.0f);
        }

        return true;
    }

    void onTick(float dt) {
        onScheduledTick(dt);
    }
};

//////////////////////////
// Modify PlayLayer      //
//////////////////////////

class $modify(MessageCheckerPlay, PlayLayer), public MessageCheckerBase {
public:
    // PlayLayer init requires args: (GJGameLevel*, bool, bool)
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) override {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        log::info("PlayLayer init");

        this->schedule(schedule_selector(MessageCheckerPlay::onTick), 1.0f);
        return true;
    }

    void onTick(float dt) {
        onScheduledTick(dt);
    }
};

//////////////////////////////
// Modify LevelEditorLayer   //
//////////////////////////////

class $modify(MessageCheckerEditor, LevelEditorLayer), public MessageCheckerBase {
public:
    // LevelEditorLayer init requires args: (GJGameLevel*, bool)
    bool init(GJGameLevel* level, bool isEditor) override {
        if (!LevelEditorLayer::init(level, isEditor))
            return false;
        log::info("LevelEditorLayer init");

        this->schedule(schedule_selector(MessageCheckerEditor::onTick), 1.0f);
        return true;
    }

    void onTick(float dt) {
        onScheduledTick(dt);
    }
};
