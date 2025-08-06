#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>

using namespace geode::prelude;

class $modify(MessageChecker_Editor, LevelEditorLayer) {
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

        req->setResponseCallback(this, SEL_HttpResponse(&MessageChecker_Editor::onMessageResponse));
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

    bool init(GJGameLevel* p0, bool p1) {
        if (!LevelEditorLayer::init(p0, p1)) return false;

        log::info("LevelEditorLayer init");

        if (!Fields::bootChecked) {
            Fields::bootChecked = true;
            log::info("boot sequence starting");

            this->schedule(schedule_selector(MessageChecker_Editor::onScheduledTick), 1.0f);
            onScheduledTick(0);
        } else {
            log::info("boot already done, skipping init check and re-running the schedule");
            this->schedule(schedule_selector(MessageChecker_Editor::onScheduledTick), 1.0f);
        }

        return true;
    }
};
