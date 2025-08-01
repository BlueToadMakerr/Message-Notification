#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool hasChecked = false;
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
            "", // description
            "GJ_messageIcon_001.png",
            false
        );
    }

    void checkMessages() {
        log::info("Checking for new messages...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("Not logged in.");
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
        log::info("Received message response.");

        if (!resp || !resp->isSucceed()) {
            log::info("Failed to contact server.");
            return;
        }

        std::string rawResponse(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (rawResponse.empty() || rawResponse == "-1") {
            log::info("No messages returned.");
            return;
        }

        auto currentRaw = split(rawResponse, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) currentClean.push_back(cleanMessage(msg));

        std::string lastSaved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto previousClean = split(lastSaved, '|');

        std::vector<std::string> newMessages;
        for (const auto& msg : currentClean) {
            if (std::find(previousClean.begin(), previousClean.end(), msg) == previousClean.end()) {
                newMessages.push_back(msg);
            }
        }

        if (!newMessages.empty()) {
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() >= 5) {
                    std::string sender = parts[1];
                    std::string subjectBase64 = parts[4];

                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    std::string subject;
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                    } else {
                        subject = "<Invalid base64>";
                        log::info("Failed to decode subject base64: {}", subjectBase64);
                    }

                    std::string title = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                    showNotification(title);
                } else {
                    showNotification("New Message!");
                }
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        } else {
            log::info("No new messages detected.");
        }

        std::string cleanedSave;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) cleanedSave += "|";
            cleanedSave += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", cleanedSave);
    }

    void runNextCheck() {
        int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
        log::info("Next check in {} seconds...", interval);

        auto delay = CCDelayTime::create(static_cast<float>(interval));
        auto call = CCCallFunc::create(this, callfunc_selector(MessageChecker::onIntervalReached));
        auto seq = CCSequence::create(delay, call, nullptr);
        this->runAction(seq);
    }

    void onIntervalReached() {
        checkMessages();
        runNextCheck();
    }

    void startMessageChecker() {
        log::info("Boot: Starting initial message check.");
        checkMessages();
        runNextCheck();
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!m_fields->hasChecked) {
            m_fields->hasChecked = true;
            startMessageChecker();
        }

        return true;
    }
};