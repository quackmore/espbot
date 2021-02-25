// dashoard.js

$(document).ready(function () {
  esp_get_info()
    .then(function () {
      hide_spinner(500);
    });
});

// device info

function esp_get_info() {
  return esp_query({
    type: 'GET',
    url: '/api/info',
    dataType: 'json',
    success: update_device_info,
    error: query_err
  });
}

function update_device_info(data) {
  $("#app_name").val(data.app_name);
  $("#app_version").val(data.app_version);
  $("#dev_name").val(data.device_name);
  $("#espbot_version").val(data.espbot_version);
  $("#api_version").val(data.api_version);
  $("#drivers_version").val(data.drivers_version);
  $("#chip_id").val(data.chip_id);
  $("#sdk_version").val(data.sdk_version);
  $("#boot_version").val(data.boot_version);
}

$('#info_edit').on('click', function () {
  if ($('#info_buttons').hasClass("d-none")) {
    $('#info_buttons').removeClass("d-none");
    $('#dev_name').removeClass("border-0");
  }
  else {
    $('#info_buttons').addClass("d-none");
    $('#dev_name').addClass("border-0");
  }
});

$('#info_reset').on('click', function () {
  show_spinner()
    .then(function () {
      esp_get_info()
        .then(function () {
          hide_spinner(500);
        });
    });
});

$('#info_save').on('click', function () {
  show_spinner()
    .then(function () {
      return esp_query({
        type: 'POST',
        url: '/api/deviceName',
        dataType: 'json',
        contentType: 'application/json',
        data: JSON.stringify({ device_name: $('#dev_name').val() }),
        success: function () {
          alert("Device name saved.");
          hide_spinner(500);
        },
        error: query_err
      });
    });
});