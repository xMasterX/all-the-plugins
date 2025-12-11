#include "nfc_comparator_led_worker.h"

static NotificationMessage blink_message_running = {
   .type = NotificationMessageTypeLedBlinkStart,
   .data.led_blink.color = LightBlue | LightGreen,
   .data.led_blink.on_time = 10,
   .data.led_blink.period = 100,
};
static const NotificationSequence blink_sequence_running = {
   &blink_message_running,
   &message_do_not_reset,
   NULL,
};

static NotificationMessage blink_message_complete = {
   .type = NotificationMessageTypeLedBlinkStart,
   .data.led_blink.color = LightGreen
};
static const NotificationSequence blink_sequence_complete = {
   &blink_message_complete,
   &message_do_not_reset,
   NULL,
};

static NotificationMessage blink_message_error = {
   .type = NotificationMessageTypeLedBlinkStart,
   .data.led_blink.color = LightRed,
};
static const NotificationSequence blink_sequence_error = {
   &blink_message_error,
   &message_do_not_reset,
   NULL,
};

void nfc_comparator_led_worker_start(
   NotificationApp* notification_app,
   NfcComparatorLedState state) {
   switch(state) {
   case NfcComparatorLedState_Running:
      notification_message_block(notification_app, &blink_sequence_running);
      break;
   case NfcComparatorLedState_Complete:
      notification_message_block(notification_app, &blink_sequence_complete);
      break;
   case NfcComparatorLedState_Error:
      notification_message_block(notification_app, &blink_sequence_error);
      break;
   default:
      break;
   }
}

void nfc_comparator_led_worker_stop(NotificationApp* notification_app) {
   notification_message_block(notification_app, &sequence_blink_stop);
}
