#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

// Split string utility
std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) result.push_back(item);
    return result;
}

class MessageChecker : public cocos2d::CCNode {
public:
    static MessageChecker* get() {
        static auto inst = new MessageChecker();
        return inst;
    }

    void checkMessagesScheduler(float) {
        checkMessages(false);
    }

    void checkMessages(bool isBoot) {
        auto mgr = GJAccountManager::sharedState();
        if (!mgr) {
            log::warn("GJAccountManager is null.");
            return;
        }

        int accountID = mgr->m_accountID;
        std::string gjp2 = mgr->m_GJP2;

        if (accountID == 0 || gjp2.empty()) {
            log::warn("Account not logged in or missing GJP2.");
            return;
        }

        log::info("Checking messages with accountID={} and GJP2={}...", accountID, gjp2.substr(0, 6) + "...");

        std::string data = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", accountID, gjp2);
        auto req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(data.c_str(), data.size());
        req->setTag(isBoot ? "boot" : "regular");
        req->setResponseCallback(MessageChecker::handleMessageResponse);
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    static void handleMessageResponse(cocos2d::extension::CCHttpClient* client, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::warn("Request failed or no response.");
            return;
        }

        std::string body(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (body.empty() || body == "-1") {
            log::info("Empty or error response received.");
            return;
        }

        auto lines = split(body, '|');
        if (lines.empty()) {
            log::info("No messages parsed.");
            return;
        }

        int lastID = Mod::get()->getSaveValue<int>("last-message-id").value_or(0);
        int newestID = lastID;
        std::vector<std::string> newMessages;

        for (const auto& msg : lines) {
            auto parts = split(msg, ':');
            if (parts.empty()) continue;

            int id = std::stoi(parts[0]);
            if (id > lastID) {
                newMessages.push_back(msg);
                if (id > newestID) newestID = id;
            }
        }

        Mod::get()->setSaveValue("last-message-id", newestID);

        std::string tag = resp->getHttpRequest()->getTag();
        bool isBoot = tag && std::string(tag) == "boot";

        if (isBoot) {
            // Show most recent message on boot
            auto parts = split(lines[0], ':');
            std::string user = parts.size() > 1 ? geode::utils::string::urlDecode(parts[1]) : "Unknown";
            std::string subject = parts.size() > 2 ? geode::utils::string::urlDecode(parts[2]) : "(No subject)";
            log::info("Boot: Showing message from {} with subject '{}'", user, subject);
            AchievementNotifier::sharedState()->notifyAchievement("Latest Message", fmt::format("From: {}\n{}", user, subject), "GJ_messageIcon_001.png", false);
        } else if (!newMessages.empty()) {
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                std::string user = parts.size() > 1 ? geode::utils::string::urlDecode(parts[1]) : "Unknown";
                std::string subject = parts.size() > 2 ? geode::utils::string::urlDecode(parts[2]) : "(No subject)";
                log::info("New message: From {} with subject '{}'", user, subject);
                AchievementNotifier::sharedState()->notifyAchievement("New Message!", fmt::format("From: {}\n{}", user, subject), "GJ_messageIcon_001.png", false);
            } else {
                log::info("Detected {} new messages.", newMessages.size());
                AchievementNotifier::sharedState()->notifyAchievement(
                    fmt::format("{} New Messages!", newMessages.size()),
                    "Check your inbox!",
                    "GJ_messageIcon_001.png",
                    false
                );
            }
        } else {
            log::info("No new messages.");
        }
    }
};

class $modify(NotifyOnBoot, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("MenuLayer loaded — scheduling message check.");

        MessageChecker::get()->checkMessages(true);
        this->schedule(schedule_selector(MessageChecker::checkMessagesScheduler), 300.0f);

        return true;
    }
};