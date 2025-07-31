#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }
        return result;
    }

    void onMessageResponse(cocos2d::CCObject* sender, void* data) {
        log::info("Received message response");

        auto* resp = static_cast<cocos2d::extension::CCHttpResponse*>(data);
        if (!resp || !resp->isSucceed()) {
            log::warn("HTTP response failed or null");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("Raw server response: {}", response);

        if (response.empty() || response == "-1") {
            log::info("No messages found or response was -1");
            return;
        }

        std::vector<std::string> messages = split(response, '|');
        log::info("Parsed {} messages", messages.size());

        if (messages.empty()) return;

        int lastMessageID = Mod::get()->getSaveValue<int>("last-message-id").value_or(0);
        log::info("Last saved message ID: {}", lastMessageID);

        std::vector<std::string> newMessages;
        int newestID = lastMessageID;

        for (const auto& msg : messages) {
            auto parts = split(msg, ':');
            if (parts.size() >= 1) {
                int id = std::stoi(parts[0]);
                log::info("Found message ID: {}", id);
                if (id > lastMessageID) {
                    newMessages.push_back(msg);
                    log::info("New message found: {}", msg);
                    if (id > newestID) newestID = id;
                }
            }
        }

        if (newMessages.empty()) {
            log::info("No new messages to notify");
            return;
        }

        log::info("Total new messages: {}", newMessages.size());
        log::info("Saving newest message ID: {}", newestID);
        Mod::get()->setSaveValue("last-message-id", newestID);

        std::string title;
        std::string desc = "Check your inbox!";
        std::string icon = "GJ_messageIcon_001.png";

        if (newMessages.size() == 1) {
            auto parts = split(newMessages[0], ':');
            std::string user = parts.size() > 1 ? geode::utils::string::urlDecode(parts[1]) : "Unknown";
            std::string subject = parts.size() > 2 ? geode::utils::string::urlDecode(parts[2]) : "(No subject)";
            title = "New Message!";
            desc = fmt::format("From: {}\n{}", user, subject);
            log::info("Notifying single message from '{}' with subject '{}'", user, subject);
        } else {
            title = fmt::format("{} New Messages!", newMessages.size());
            log::info("Notifying {} new messages", newMessages.size());
        }

        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(), desc.c_str(), icon.c_str(), false
        );
    }

    void checkMessages(float = 0.f) {
        log::info("Initiating message check...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::warn("User is not logged in; skipping message check");
            return;
        }

        std::string postData = fmt::format(
            "accountID={}&gjp2={}&secret=Wmfd2893gb7",
            acc->m_accountID, acc->m_GJP2
        );

        log::info("Sending POST request with accountID {}", acc->m_accountID);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.size());
        req->setResponseCallback(this, cocos2d::extension::SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("MessageChecker initialized");

        // Run once on startup
        this->checkMessages();

        // Schedule every 5 minutes
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f);

        return true;
    }
};
