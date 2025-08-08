#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/base64.hpp>
#include <chrono>

using namespace geode::prelude;

static int menuInit = 0;

struct MessageData {

    int messageID = -1;
    int accountID = -1;
    int playerID = -1;
    std::string title;
    std::string content;
    std::string username;
    std::string age;
    bool read = false;
    bool sender = false;
    
    static MessageData parseInto(const std::string& data) {
        MessageData messageData;
        auto split = utils::string::split(data, ":");

        int key = -1;
        for (const auto& str : split) {
            if (key == -1) {
                key = utils::numFromString<int>(str).unwrapOr(-2);
                continue;
            }
            // added all the keys in case you want to expand upon what is shown.
            switch (key) {
                case 1:
                    messageData.messageID = utils::numFromString<int>(str).unwrapOr(-1);
                    break;
                case 2:
                    messageData.accountID = utils::numFromString<int>(str).unwrapOr(-1);
                    break;
                case 3:
                    messageData.playerID = utils::numFromString<int>(str).unwrapOr(-1);
                    break;
                case 4:
                    messageData.title = utils::base64::decodeString(str).unwrapOrDefault();
                    break;
                case 5:
                    messageData.content = utils::base64::decodeString(str).unwrapOrDefault();
                    break;
                case 6:
                    messageData.username = str;
                    break;
                case 7:
                    messageData.age = str;
                    break;
                case 8:
                    messageData.read = utils::numFromString<int>(str).unwrapOrDefault();
                    break;
                case 9:
                    messageData.sender = utils::numFromString<int>(str).unwrapOrDefault();
                    break;
            }
            key = -1;
        }
        return messageData;
    }
};

class MessageHandler : public CCNode {
    
    std::chrono::steady_clock::time_point m_nextCheck = std::chrono::steady_clock::now();
    std::shared_ptr<EventListener<web::WebTask>> m_listener;
    bool m_checkedMenuLayer;

    void update(float dt) {
        int interval = Mod::get()->getSettingValue<int>("check-interval");
        auto now = std::chrono::steady_clock::now();

        if (MenuLayer::get()) {
            if (!m_checkedMenuLayer) {
                m_nextCheck = std::chrono::steady_clock::now();
                m_checkedMenuLayer = true;
                menuInit = 1;
                checkMessages();
            }
        }
        else {
            m_checkedMenuLayer = false;
        }

        if (now >= m_nextCheck) {
            checkMessages();
            m_nextCheck = now + std::chrono::seconds(interval);
            log::debug("Performing message check, next check scheduled in {} seconds.", interval);
        }
    }

    void checkMessages() {
        if (Mod::get()->getSettingValue<bool>("stop-notifications")
            || (PlayLayer::get() && Mod::get()->getSettingValue<bool>("disable-while-playing"))
            || (LevelEditorLayer::get() && Mod::get()->getSettingValue<bool>("disable-while-editing"))) {
            return;
        }

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::debug("User not logged in, skipping check.");
            return;
        }

        auto req = web::WebRequest();
        req.bodyString(fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2));
        req.userAgent("");
        req.header("Content-Type", "application/x-www-form-urlencoded");

        m_listener = std::make_shared<EventListener<web::WebTask>>();
        m_listener->bind([this] (web::WebTask::Event* e) {
            if (web::WebResponse* res = e->getValue()) {
                if (res->ok() && res->string().isOk()) {
                    onMessageResponse(res->string().unwrap());
                }
                else log::debug("Message request failed: {}", res->code());
            }
        });

        auto downloadTask = req.post("https://www.boomlings.com/database/getGJMessages20.php");
        m_listener->setFilter(downloadTask);
    }

    void onMessageResponse(const std::string& data) {
        std::vector<std::string> split = utils::string::split(data, "|");
        // reverses the messages so they are in ascending order, allows us to count how many are new
        std::reverse(split.begin(), split.end());

        int latestID = Mod::get()->getSavedValue<int>("latest-id", 0);
        int newMessages = 0;
        
        for (const auto& str : split) {
            MessageData data = MessageData::parseInto(str);
            if (data.messageID > latestID) {
                latestID = data.messageID;
                newMessages++;
            }
        }
        
        // stores the message ID as they are always incremental, no need to store the whole message.
        Mod::get()->setSavedValue("latest-id", latestID);

        if (initFlag == 0) {
            return;
        }

        if (newMessages > 1) {
            showNotification(fmt::format("{} New Messages!", newMessages), "Check them out!");
        }
        else if (newMessages == 1) {
            MessageData data = MessageData::parseInto(split[split.size()-1]);
            showNotification(fmt::format("New Message from: {}", data.username), data.title);
        }
    }

    void showNotification(const std::string& title, const std::string& content) {
        // use quest bool so it doesn't act like an actual achievement alert saying an icon was unlocked.
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            content.c_str(),
            "accountBtn_messages_001.png",
            true
        );
    }

public:
    static MessageHandler* create() {
        auto ret = new MessageHandler();
        ret->autorelease();
        return ret;
    }
};

$execute {
     Loader::get()->queueInMainThread([]{
          CCScheduler::get()->scheduleUpdateForTarget(MessageHandler::create(), INT_MAX, false);
     });
}

