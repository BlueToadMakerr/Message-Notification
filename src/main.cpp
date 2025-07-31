#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>
#include <sstream>

using namespace geode::prelude;

class $modify(StartupMessageMod, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        MessageChecker::get()->checkMessages(true); // run once on boot
        this->schedule(schedule_selector(MessageChecker::checkMessagesScheduler), 300.0f); // 5 min

        return true;
    }
};

// Utility: split a string
static std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
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
        std::string gjp2 = Mod::get()->getSaveValue<std::string>("gjp2").value_or("");
        int accountID = Mod::get()->getSaveValue<int>("accountID").value_or(0);
        if (gjp2.empty() || accountID == 0) {
            log::warn("Missing GJP2/accountID; skipping message check.");
            return;
        }

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
        if (!resp || !resp->isSucceed()) return;
        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (response.empty() || response == "-1") return;

        auto messages = split(response, '|');
        if (messages.empty()) return;

        int lastMessageID = Mod::get()->getSaveValue<int>("last-message-id").value_or(0);
        std::vector<std::string> newMessages;
        int newestID = lastMessageID;

        for (const auto& msg : messages) {
            auto parts = split(msg, ':');
            if (parts.size() >= 1) {
                int id = std::stoi(parts[0]);
                if (id > lastMessageID) {
                    newMessages.push_back(msg);
                    if (id > newestID) newestID = id;
                }
            }
        }

        if (newMessages.empty()) return;
        Mod::get()->setSaveValue("last-message-id", newestID);

        std::string title;
        std::string desc;
        std::string icon = "GJ_messageIcon_001.png";

        // Display latest message even if not new, only on boot
        std::string tag = resp->getHttpRequest()->getTag();
        bool isBoot = tag && std::string(tag) == "boot";

        if (newMessages.size() == 1 || isBoot) {
            std::string latest = newMessages.back();
            auto parts = split(latest, ':');
            std::string user = parts.size() > 1 ? geode::utils::string::urlDecode(parts[1]) : "Unknown";
            std::string subject = parts.size() > 2 ? geode::utils::string::urlDecode(parts[2]) : "(No subject)";
            title = "New Message!";
            desc = fmt::format("From: {}\n{}", user, subject);
        } else {
            title = fmt::format("{} New Messages!", newMessages.size());
            desc = "Check your inbox!";
        }

        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), desc.c_str(), icon.c_str(), false);
    }
};