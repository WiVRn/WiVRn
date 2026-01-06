import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami 
import org.kde.coreaddons as KCoreAddons
import org.kde.kirigamiaddons.formcard as FormCard

FormCard.AboutPage {
	aboutData: KCoreAddons.AboutData
	donateUrl: "https://opencollective.com/wivrn"
	getInvolvedUrl: ""

	footer: Controls.DialogButtonBox {
		standardButtons: Controls.DialogButtonBox.NoButton
		onAccepted: applicationWindow().pageStack.pop()

		Controls.Button {
			text: i18nc("go back to the home page", "Back")
			icon.name: "go-previous"
			Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
		}
	}

	Shortcut {
		sequences: [StandardKey.Cancel]
		onActivated: {if (isCurrentPage) applicationWindow().pageStack.pop();}
	}
}
