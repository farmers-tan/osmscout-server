import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import osmscout.theme 1.0
import "."

Dialog {
    id: page
    dialogue: false
    title: qsTr("About") + " " + programName

    property string mainText: "<p>" +
                              qsTr("The server has to be used with the client program that is able to display maps, query for searches and " +
                                   "display selected routes. The server is a thin layer exposing functionality of <i>libosmscout</i>, " +
                                   "<i>Mapnik</i>, <i>Valhalla</i>, <i>libpostal</i>, <i>Geocoder-NLP</i> and providing data for " +
                                   "<i>Mapbox GL</i> through web service. " +
                                   "This server can be used as a drop-in replacement for " +
                                   "online services providing maps.") + "</p><br>" +
                              "<p>" + qsTr("Copyright:") + " 2016-2018 <a href='https://github.com/rinigus'>Rinigus</a><br></p>" +
                              qsTr("License: LGPL") + "<br><br>" +

                              "Donations: <a href='https://liberapay.com/rinigus'>Liberapay</a> " +
                              "<a href='http://rinigus.github.io/donate-bitcoin'>Bitcoin</a>" +
                              "<br><br>" +

                              "<b>" + qsTr("Translations") + "</b><br><br>" +
                              "Carmen F. B. @carmenfdezb: Spanish<br>" +
                              "Lukáš Karas @Karry: Czech<br>" +
                              "Åke Engelbrektson @eson57: Swedish<br>" +
                              "Ricardo Breitkopf @monkeyisland: German<br>" +
                              "Nathan Follens @pljmn: Dutch (NL and BE)<br>" +
                              "@Sagittarii: French<br>" +
                              "Oleg Artobolevsky @XOleg: Russian<br>" +
                              "A @atlochowski: Polish<br>" +
                              "Peer-Atle Motland @Pam: Norwegian Bokmål<br>" +
                              "Matti Lehtimäki @mal: Finnish<br>" +

                              "<br><b>" + qsTr("Maps") + "</b><br>" +
                              "<p>" +
                              qsTr("Maps are hosted by " +
                                   "Natural Language Processing Centre (Faculty of Informatics, Masaryk University, Brno, Czech Republic) " +
                                   "through modRana.org data repository.") +
                              "</p><br>" +
                              "<p>" +
                              qsTr("Map data from OpenStreetMap, Open Database License 1.0. Maps are converted to a suitable format " +
                                   "from downloaded extracts and/or using polygons as provided by Geofabrik GmbH.") +
                              "</p>" +

                              "<br><b>" + qsTr("Libpostal") + "</b><br>" +
                              "<p>" +
                              qsTr("Please note that <i>libpostal</i> is developed to be used with the fine tuned " +
                                   "model covering the World. The country-based models were developed to use " +
                                   "<i>libpostal</i> on mobile devices and have not been specifically tuned. Please submit the issues " +
                                   "with libpostal performance on country-based models to <i>OSM Scout Server</i> or " +
                                   "<i>geocoder-nlp</i> github projects.") +
                              "</p>" +

                              "<br><b>" + qsTr("Acknowledgments") + "</b><br><br>" +
                              "Used external projects:<br>" +
                              "<a href='http://libosmscout.sourceforge.net/'>libosmscout</a><br>" +
                              "<a href='https://github.com/openvenues/libpostal'>libpostal</a><br>" +
                              "<a href='http://mapnik.org'>Mapnik</a><br>" +
                              "<a href='https://github.com/valhalla/valhalla'>Valhalla</a><br>" +
                              "<a href='https://github.com/rinigus/geocoder-nlp'>Geocoder-NLP</a><br>" +
                              "<a href='https://www.gnu.org/software/libmicrohttpd'>GNU Libmicrohttpd</a><br>" +
                              "<a href='https://github.com/LuminosoInsight/langcodes'>langcodes</a><br><br>" +
                              "Hosting of maps: Natural Language Processing Centre (Faculty of Informatics, " +
                              "Masaryk University, Brno, Czech Republic) through <a href='http://modrana.org'>modRana.org</a><br><br>" +
                              "Fonts: <a href='https://www.google.com/get/noto/'>Noto fonts</a> (SIL Open Font License, Version 1.1)<br>"


    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    contentHeight: column.height + Theme.paddingLarge

    Column {
        id: column
        x: Theme.horizontalPageMargin
        width: page.width - 2*x

        spacing: Theme.paddingLarge

        Label {
            text: qsTr("version") + ": " + programVersion
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: programName + " " + qsTr("is a server that provides offline map services")
            wrapMode: Text.WordWrap
            width: column.width
        }

        Label {
            text: qsTr("User's guide is available <a href='https://rinigus.github.io/osmscout-server'>online</a>")
            wrapMode: Text.WordWrap
            width: column.width
            onLinkActivated: Qt.openUrlExternally(link)
        }

        Label {
            text: mainText
            wrapMode: Text.WordWrap
            width: column.width
            onLinkActivated: Qt.openUrlExternally(link)
        }
    }
}
