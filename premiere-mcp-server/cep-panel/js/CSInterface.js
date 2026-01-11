/**
 * CSInterface - Minimal implementation for CEP panels
 * This provides the core functionality needed to communicate with ExtendScript
 */

function CSInterface() {}

/**
 * Evaluates a JavaScript script in the ExtendScript engine
 * @param script The script to evaluate
 * @param callback Callback function with the result
 */
CSInterface.prototype.evalScript = function(script, callback) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.evalScript(script, callback);
    } else {
        // Fallback for testing outside CEP environment
        console.log("[CSInterface] evalScript called (not in CEP environment):", script);
        if (callback) {
            callback("EvalScript error.");
        }
    }
};

/**
 * Gets the host environment information
 */
CSInterface.prototype.getHostEnvironment = function() {
    if (window.__adobe_cep__) {
        var hostEnv = window.__adobe_cep__.getHostEnvironment();
        return JSON.parse(hostEnv);
    }
    return {
        appName: "Unknown",
        appVersion: "0.0"
    };
};

/**
 * Gets the system path
 * @param pathType The type of path to get
 */
CSInterface.prototype.getSystemPath = function(pathType) {
    if (window.__adobe_cep__) {
        return window.__adobe_cep__.getSystemPath(pathType);
    }
    return "";
};

/**
 * Opens a URL in the default browser
 * @param url The URL to open
 */
CSInterface.prototype.openURLInDefaultBrowser = function(url) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.openURLInDefaultBrowser(url);
    } else {
        window.open(url);
    }
};

/**
 * Gets the current extension ID
 */
CSInterface.prototype.getExtensionID = function() {
    if (window.__adobe_cep__) {
        return window.__adobe_cep__.getExtensionId();
    }
    return "com.moshbrosh.mcpbridge.panel";
};

/**
 * Closes the current extension
 */
CSInterface.prototype.closeExtension = function() {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.closeExtension();
    }
};

/**
 * Requests opening an extension
 * @param extensionId The extension ID to open
 * @param params Optional parameters
 */
CSInterface.prototype.requestOpenExtension = function(extensionId, params) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.requestOpenExtension(extensionId, params || "");
    }
};

/**
 * Dispatches an event
 * @param event The event to dispatch
 */
CSInterface.prototype.dispatchEvent = function(event) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.dispatchEvent(event);
    }
};

/**
 * Adds an event listener
 * @param type The event type
 * @param listener The listener function
 * @param obj Optional object for context
 */
CSInterface.prototype.addEventListener = function(type, listener, obj) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.addEventListener(type, listener, obj);
    }
};

/**
 * Removes an event listener
 * @param type The event type
 * @param listener The listener function
 * @param obj Optional object for context
 */
CSInterface.prototype.removeEventListener = function(type, listener, obj) {
    if (window.__adobe_cep__) {
        window.__adobe_cep__.removeEventListener(type, listener, obj);
    }
};

// System path constants
CSInterface.prototype.EXTENSION_PATH = "extension";
CSInterface.prototype.USER_DATA_PATH = "userData";

// Export for Node.js environments
if (typeof module !== "undefined" && module.exports) {
    module.exports = CSInterface;
}
