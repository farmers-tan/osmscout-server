import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {

    id: dialog
    allowedOrientations : Orientation.All

    property bool backendSelectionPossible: false

    // To enable PullDownMenu, place our content in a SilicaFlickable
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge
            anchors.margins: Theme.horizontalPageMargin

            DialogHeader {
                title: qsTr("Settings")
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                ComboBox {
                    id: unitsBox
                    label: qsTr("Units")
                    menu: ContextMenu {
                        MenuItem { text: qsTr("Metric") }
                        MenuItem { text: qsTr("Imperial") }
                    }
                    Component.onCompleted: {
                        unitsBox.currentIndex = settings.unitIndex();
                    }
                }

                Label {
                    text: qsTr("Units used in the graphical user interface of the server. The units will change only after you apply the settings.")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                ComboBox {
                    id: preferredLanguageSelection
                    label: qsTr("Language")
                    enabled: manager.ready

                    menu: ContextMenu {
                        MenuItem { text: qsTr("Default") }
                        MenuItem { text: qsTr("English") }
                    }

                    Component.onCompleted: {
                        currentIndex = settings.valueInt(settingsGeneralPrefix + "language")
                    }
                }

                Label {
                    text: qsTr("Preferred language for location names shown in rendered maps or in the returned search results. " +
                               "When possible, this language will be used. When set to <i>Default</i>, OpenStreetMap name will be used " +
                               "which usually defaults to local language of the displayed country."
                               )
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            ElementSelector {
                id: eMapsRoot
                key: settingsMapManagerPrefix + "root"
                mainLabel: qsTr("Maps storage")
                secondaryLabel: qsTr("Folder to store maps.<br><b>NB!</b> This folder will be fully managed by OSM Scout Server. " +
                                     "Please <b>allocate separate, empty folder</b> that OSM Scout Server could use. " +
                                     "This includes deleting all files within that folder when requested by you during cleanup or " +
                                     "map updates.<br>" +
                                     "<i>Example:</i> Maps under <i>~nemo</i>")
                directory: true
            }

            ///////////////////////////////////////
            /// systemd support
            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                TextSwitch {
                    id: systemdEnable
                    text: qsTr("Automatic activation")
                    enabled: manager.ready

                    Component.onCompleted: {
                        checked = systemd_service.enabled
                    }
                }

                Label {
                    text: qsTr("When enabled, OSM Scout Server will be activated automatically by any client accessing it. " +
                               "Automatically started server will work in the background."
                               )
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }

                Label {
                    text: qsTr("It is recommended to enable automatic activation to simplify the access to the server.")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }

                Label {
                    text: qsTr("For technical details, created files, and how to remove them if needed, see corresponding section " +
                               "of the <a href='https://rinigus.github.io/osmscout-server/en/#implementation-of-automatic-activation'>User's Guide</a>.")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                    linkColor: Theme.primaryColor
                    onLinkActivated: Qt.openUrlExternally(link)
                }

                ComboBox {
                    id: idleTimeout
                    label: qsTr("Idle timeout")
                    enabled: manager.ready && systemdEnable.checked

                    property var timeouts: [
                        {"value": 900, "desc": qsTr("15 minutes") },
                        {"value": 1800, "desc": qsTr("30 minutes") },
                        {"value": 3600, "desc": qsTr("1 hour")},
                        {"value": 7200, "desc": qsTr("2 hours")},
                        {"value": 14400, "desc": qsTr("4 hours")},
                        {"value": 28800, "desc": qsTr("8 hours")},
                        {"value": 86400, "desc": qsTr("24 hours")},
                        {"value": -1, "desc": qsTr("No timeout")}
                    ]


                    menu: ContextMenu {
                        MenuItem { text: idleTimeout.timeouts[0]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[1]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[2]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[3]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[4]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[5]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[6]["desc"] }
                        MenuItem { text: idleTimeout.timeouts[7]["desc"] }
                    }

                    function apply() {
                        settings.setValue(settingsRequestMapperPrefix + "idle_timeout", timeouts[currentIndex]["value"])
                    }

                    Component.onCompleted: {
                        var timeout = settings.valueInt(settingsRequestMapperPrefix + "idle_timeout")
                        if (timeout < 0) {
                            currentIndex = 7
                            return
                        }

                        for (var i = 0; i < timeouts.length-2; i++)
                            if ( timeout <= timeouts[i]["value"] ) {
                                currentIndex = i
                                return
                            }

                        currentIndex = timeouts.length-2
                    }
                }

                Label {
                    text: qsTr("When started automatically, the server will shutdown itself after " +
                               "not receiving any requests for longer than the idle timeout")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            ///////////////////////////////////////

            SectionHeader {
                text: qsTr("Profiles")
                visible: settings.profilesUsed
            }

            Label {
                text: qsTr("Active backends are set by the profile. " +
                           "If you wish to change the backend selection, please set the corresponding profile " +
                           "or set profile to <i>Custom</i>.")
                visible: settings.profilesUsed
                x: Theme.horizontalPageMargin
                width: parent.width-2*x
                wrapMode: Text.WordWrap
                //font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            SectionHeader {
                text: qsTr("Rendering")
            }

            Label {
                text: qsTr("This server allows you to select between two backends to draw the maps: <i>libosmscout</i> and <i>mapnik</i>.")
                x: Theme.horizontalPageMargin
                width: parent.width-2*x
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            ElementSwitch {
                id: eMapnik
                activeState: dialog.backendSelectionPossible
                key: settingsMapnikPrefix + "use_mapnik"
                mainLabel: qsTr("Use Mapnik for rendering maps")
                secondaryLabel: qsTr("When selected, Mapnik will be used to render maps. " +
                                     "Note that it requires additional databases for World coastlines and countries.<br>")
            }

            SectionHeader {
                text: qsTr("Geocoder")
            }

            Label {
                text: qsTr("Geocoder is responsible for resolving search requests. " +
                           "For that, it parses the search string and finds the corresponding objects on a map. " +
                           "This server allows you to select between two geocoder backends: <i>geocoder-nlp</i> and " +
                           "<i>libosmscout</i>. ")
                x: Theme.horizontalPageMargin
                width: parent.width-2*x
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            ElementSwitch {
                id: eGeocoderNLP
                activeState: dialog.backendSelectionPossible
                key: settingsGeomasterPrefix + "use_geocoder_nlp"
                mainLabel: qsTr("Use geocoder-nlp with libpostal as a geocoder")
                secondaryLabel: qsTr("When selected, a libpostal-based geocoder will be used to resolve all search requests. " +
                                     "Note that it requires additional databases for language, user input parsing, and geocoding.<br>" +
                                     "NB! If you select <i>geocoder-nlp</i>, please specify languages that should be used for " +
                                     "address parsing in the backend settings below. Otherwise, the server could use large amounts of RAM.")
            }

            SectionHeader {
                text: qsTr("Routing Engine")
            }

            Label {
                text: qsTr("Routing engine is responsible for calculating routes between origin and destination. " +
                           "This server allows you to select between two routing engines: <i>Valhalla</i> and " +
                           "<i>libosmscout</i>. ")
                x: Theme.horizontalPageMargin
                width: parent.width-2*x
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }

            ElementSwitch {
                id: eValhalla
                activeState: dialog.backendSelectionPossible
                key: settingsValhallaPrefix + "use_valhalla"
                mainLabel: qsTr("Use Valhalla as routing engine")
                secondaryLabel: qsTr("When selected, Valhalla will be used to calculate the routing instructions.")
            }

            SectionHeader {
                text: qsTr("Backend settings")
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                Button {
                    text: qsTr("Mapnik")
                    onClicked: pageStack.push(Qt.resolvedUrl("MapnikPage.qml"))
                    preferredWidth: Theme.buttonWidthLarge
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("Map rendering settings for <i>mapnik</i> backend")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                Rectangle {
                    width: parent.width
                    height: Theme.paddingLarge
                    color: "transparent"
                }

                Button {
                    text: qsTr("Geocoder-NLP")
                    onClicked: pageStack.push(Qt.resolvedUrl("GeocoderPage.qml"))
                    preferredWidth: Theme.buttonWidthLarge
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("Selection of languages used for address parsing and other settings of <i>geocoder-nlp</i> backend")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                Rectangle {
                    width: parent.width
                    height: Theme.paddingLarge
                    color: "transparent"
                }

                Button {
                    text: qsTr("Valhalla")
                    onClicked: pageStack.push(Qt.resolvedUrl("ValhallaPage.qml"))
                    preferredWidth: Theme.buttonWidthLarge
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("Routing engine settings for <i>Valhalla</i> backend")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                anchors.margins: Theme.horizontalPageMargin

                Rectangle {
                    width: parent.width
                    height: Theme.paddingLarge
                    color: "transparent"
                }

                Button {
                    text: qsTr("OSM Scout library")
                    onClicked: pageStack.push(Qt.resolvedUrl("OSMScoutPage.qml"))
                    preferredWidth: Theme.buttonWidthLarge
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("OSM Scout library (<i>libosmscout</i>) settings. " +
                               "The library can be used for drawing maps, search, and routing.")
                    x: Theme.horizontalPageMargin
                    width: parent.width-2*x
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            SectionHeader {
                text: qsTr("Miscellaneous")
            }

            ElementSwitch {
                id: eLogInfo
                key: settingsGeneralPrefix + "logInfo"
                mainLabel: qsTr("Log info messages")
                secondaryLabel: qsTr( "When disabled, INFO messages will not be logged in Events log. " +
                                     "Exception is loading of the database which is shown always." )
            }

            ElementEntry {
                id: eRollSize
                key: settingsGeneralPrefix + "rollingLoggerSize"
                mainLabel: qsTr("Events log size")
                secondaryLabel: qsTr("Number of events shown in the main page")
                validator: IntValidator { bottom: 3; top: 25; }
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }

            ElementSwitch {
                id: eLogSession
                key: settingsGeneralPrefix + "logSession"
                mainLabel: qsTr("Log messages into session log file")
                secondaryLabel: qsTr("When enabled, the messages are logged into a session log file. " +
                                     "The log file is at .cache/harbour-osmscout-server directory. " +
                                     "Use this setting only for debugging purposes. With this setting enabled, " +
                                     "info messages will be logged and shown on events irrespective to the " +
                                     "settings above. This allows you to temporary enable full logging and disable it " +
                                     "when the required session log file was produced." )
            }
        }

        VerticalScrollDecorator {}
    }

    function checkState()
    {
        dialog.backendSelectionPossible = !settings.profilesUsed
    }

    Component.onCompleted: {
        checkState()
    }

    Connections {
        target: settings
        onProfilesUsedChanged: checkState()
    }

    onAccepted: {
        eLogInfo.apply()
        eRollSize.apply()
        eLogSession.apply()

        /// preferred languages are done by combo box, have to apply manually
        settings.setValue(settingsGeneralPrefix + "language", preferredLanguageSelection.currentIndex)

        eMapsRoot.apply()
        eGeocoderNLP.apply()
        eMapnik.apply()
        eValhalla.apply()

        /// units are done by combo box, have to apply manually
        /// units are changed the last
        settings.setValue(settingsGeneralPrefix + "units", unitsBox.currentIndex)

        systemd_service.enabled = systemdEnable.checked
        idleTimeout.apply()
    }
}
