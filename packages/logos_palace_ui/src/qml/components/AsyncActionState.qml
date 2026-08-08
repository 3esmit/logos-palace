import QtQuick

Text {
    property string receipt: ""
    property bool pending: false

    objectName: "palaceAsyncActionState"
    color: receipt.indexOf("rejected=") === 0 ? "#a33b2b" : "#4f7650"
    elide: Text.ElideRight
    text: pending ? "Waiting for LEZ…" : receipt
    visible: pending || receipt.length > 0
}
