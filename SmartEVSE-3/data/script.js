//Globals
var websock = null;
var wsUri;
var sw_rev = ""
var hw_rev = "";
var language = "";
var langMap;
var timeoutInterval;
var inApMode = false;
var timerRefreshStats;
var highResolution = false;

//EVSE Control
var pp_limit;
var chargingTime;
var evseActive;
var vehicleCharging;
var prevCurrent;
var prevMaxCurrent;
var currentModalOpen = false;

//Users
var timezone;
var userdata = [];
var page = 1;
var haspages;
var usertable = false;
var editoropen = false;

//Log
var logdata;
var logdataraw;
var logtable = false;
var maxLog;

//Settings
var utcSeconds;
var recordstorestore = 0;
var slot = 0;
var page = 1;
var haspages;
var file = {};
var completed = false;
var wsUri;
var wifiSsidBackupAP = "";
var wifiPassBackupAP = "";
var wifiSsidBackupSTA = "";
var wifiPassBackupSTA = "";
var jsonUpdateList;
var fwUpdateIndex = 0;

//Status
var refreshSeconds;

window.onload = function () {

}

//Script data for EVSE Control
function loadEVSEControl() {
  getEvseData();
  document.getElementById("evseContent").style.display = "block";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
  document.getElementById("loginContent").style.display = "none";
  document.getElementById("syslogContent").style.display = "none";
  clearInterval(timerRefreshStats);
  closeNav();
  timerRefreshEvseData = setInterval(getEvseData, 2000);
}

function getEvseData() {
  websock.send("{\"command\":\"getevsedata\"}");
  return;
}
function listEVSEData(obj) {
  chargingTime = obj.evse_charging_time;
  evseActive = obj.evse_active;
  document.getElementById("evse_charging_time").innerHTML = getTimeFormat(obj.evse_charging_time);
  document.getElementById("evse_current_limit").innerHTML = obj.evse_current_limit + " A";
  if (obj.evse_rse_status === true) {
    document.getElementById("evse_current_limit").innerHTML = "RSE Active <span class=\"glyphicon glyphicon-flash\" style=\"color:red\"></span>&nbsp;" + obj.evse_current_limit + " / " + obj.evse_rse_current_before + " A (" + obj.evse_rse_value + "%)";
  }
  if (obj.evse_current_limit > pp_limit) {
    document.getElementById("evse_current_limit").innerHTML = "PP-Limit <span class=\"glyphicon glyphicon-flash\" style=\"color:orange\"></span>&nbsp;" + pp_limit + " A / " + obj.evse_current_limit + " A";
  }

  document.getElementById("evse_current").innerHTML = obj.evse_current + " kW";
  document.getElementById("evse_charged_kwh").innerHTML = obj.evse_charged_kwh + " kWh / " + obj.evse_charged_amount + " €";
  document.getElementById("evse_charged_mileage").innerHTML = obj.evse_charged_mileage + " km";
  if (obj.evse_active === false) {		//EVSE not active
    $("#evseActive").addClass('hidden');
    $("#evseNotActive").removeClass('hidden');
  }
  else {	//EVSE active
    $("#evseNotActive").addClass('hidden');
    $("#evseActive").removeClass('hidden');
  }
  if (obj.evse_always_active || obj.evse_timer_active) { //Always Active Mode
    $("#evseNotActive").addClass('hidden');
    $("#evseActive").addClass('hidden');
  }
  if (obj.evse_vehicle_state === 0) {	//modbus error
    vehicleCharging = false;
    $("#carStatusDetected").addClass('hidden');
    $("#carStatusCharging").addClass('hidden');
    $("#carStatusReady").removeClass('hidden');
    document.getElementById("evse_vehicle_state").innerHTML = translate("ec_error");
  }
  if (obj.evse_vehicle_state === 1) {	//Ready
    vehicleCharging = false;
    $("#carStatusDetected").addClass('hidden');
    $("#carStatusCharging").addClass('hidden');
    $("#carStatusReady").removeClass('hidden');
    document.getElementById("evse_vehicle_state").innerHTML = translate("ec_ready");
  }
  if (obj.evse_vehicle_state === 2) {	//Vehicle Detected
    vehicleCharging = false;
    $("#carStatusReady").addClass('hidden');
    $("#carStatusCharging").addClass('hidden');
    $("#carStatusDetected").removeClass('hidden');
    document.getElementById("evse_vehicle_state").innerHTML = translate("ec_connected");
  }
  if (obj.evse_vehicle_state === 3) {	//Vehicle charging
    vehicleCharging = true;
    $("#carStatusReady").addClass('hidden');
    $("#carStatusDetected").addClass('hidden');
    $("#carStatusCharging").removeClass('hidden');
    document.getElementById("evse_vehicle_state").innerHTML = translate("ec_charging");
  }
  if (obj.evse_timer_active === true) {  //Timer Symbol
    document.getElementById("evse_vehicle_state").innerHTML += "&nbsp;<span class=\"glyphicon glyphicon-time\" style=\"color:green\"></span>";
  }
  document.getElementById("evse_charging_time").innerHTML = getTimeFormat(obj.evse_charging_time);
  if (obj.ap_mode === true) {
    syncBrowserTime(false);
  }
  if (currentModalOpen === false) {
    if (document.getElementById("currentSlider").value != prevCurrent) {
      document.getElementById("currentSlider").value = obj.evse_current_limit;
      document.getElementById("slider_current").innerHTML = obj.evse_current_limit + " A";
    }
    prevCurrent = obj.evse_current_limit;
    if (document.getElementById("currentSlider").max != prevMaxCurrent) {
      document.getElementById("currentSlider").max = obj.evse_maximum_current;
    }
    prevMaxCurrent = obj.evse_maximum_current;
  }

  if (obj.evse_slider_status === false) {
    document.getElementById("currentSlider").disabled = true;
    document.getElementById("currentModalSaveButton").disabled = true;
    document.getElementById("slider_current").innerHTML = obj.evse_current_limit + " A <br>"// <br><span style=\"color:red\">Manual current specification in remote mode disabled!</span>";
    document.getElementById("slider_current").innerHTML += translate("ec_remote_curr_dis");
  } else {
    document.getElementById("currentSlider").disabled = false;
    document.getElementById("currentModalSaveButton").disabled = false;
  }

  if (obj.evse_remote_controlled) {
    if (obj.evse_disabled_by_remote_hearbeat === true) {
      document.getElementById("dangerRemoteHeartbeat").style.display = "block";
      document.getElementById("successRemoteHeartbeat").style.display = "none";
    }
    else {
      document.getElementById("dangerRemoteHeartbeat").style.display = "none";
      document.getElementById("successRemoteHeartbeat").style.display = "block";
    }
  }

}
function handleSlider(value) {
  if (highResolution) {
    document.getElementById("slider_current").innerHTML = parseFloat(value).toFixed(1) + " A";
  }
  else {
    document.getElementById("slider_current").innerHTML = parseFloat(value) + " A";
  }
  
}
function setEVSECurrent() {
  var currentToSet = document.getElementById("currentSlider").value;
  if (highResolution) {
    currentToSet = parseFloat(currentToSet) * 100.0;
  }
  websock.send("{\"command\":\"setcurrent\", \"current\":" + currentToSet + "}");
  $("#currentModal").modal("hide");
  currentModalOpen = false;
}
function abortCurrentModal() {
  currentModalOpen = false;
}
function activateEVSE() {
  if (document.getElementById("buttonActivate").className.includes("disabled") === false) {
    websock.send("{\"command\":\"activateevse\"}");
    $("#buttonActivate").addClass('disabled');
    setTimeout(function () { $("#buttonActivate").removeClass("disabled"); }, 4000);
  }
}
function deactivateEVSE() {
  if (document.getElementById("buttonDeactivate").className.includes("disabled") === false) {
    websock.send("{\"command\":\"deactivateevse\"}");
    $("#buttonDeactivate").addClass('disabled');
    setTimeout(function () { $("#buttonDeactivate").removeClass('disabled'); }, 4000);
  }
}
function getTimeFormat(millisec) {
  var seconds = (millisec / 1000).toFixed(0);
  var minutes = Math.floor(seconds / 60);
  var hours = "";
  if (minutes > 59) {
    hours = Math.floor(minutes / 60);
    hours = (hours >= 10) ? hours : "0" + hours;
    minutes = minutes - (hours * 60);
    minutes = (minutes >= 10) ? minutes : "0" + minutes;
  }

  seconds = Math.floor(seconds % 60);
  seconds = (seconds >= 10) ? seconds : "0" + seconds;
  if (hours != "") {
    return hours + ":" + minutes + ":" + seconds;
  }
  return minutes + ":" + seconds;
}

//Script data for Users
function loadUsers() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "block";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
  document.getElementById("syslogContent").style.display = "none";
  closeNav();
  clearInterval(timerRefreshStats);
  clearInterval(timerRefreshEvseData);
  userdata = [];
  websock.send("{\"command\":\"userlist\", \"page\":" + page + "}");
}
function listSCAN(obj) {
  if (obj.known === 1) {
    $(".fooicon-remove").click();
    document.querySelector('input.form-control[type=text]').value = obj.uid;
    $(".fooicon-search").click();
  } else {
    $(".footable-add").click();
    document.getElementById("uid").value = obj.uid;
    document.getElementById("picctype").value = obj.type;
    document.getElementById("username").value = obj.user;
    document.getElementById("acctype").value = obj.acctype;
  }
}

function getnextpage() {
  document.getElementById("loadpages").innerHTML = "Loading " + page + "/" + haspages;
  if (page < haspages) {
    page = page + 1;
    var commandtosend = {};
    commandtosend.command = "userlist";
    commandtosend.page = page;
    websock.send(JSON.stringify(commandtosend));
  }
}

function builduserdata(obj) {
  userdata = obj.list;
}

function initUserTable() {
  jQuery(function ($) {
    var $modal = $("#editor-modal"),
      $editor = $("#editor"),
      $editorTitle = $("#editor-title"),
      ft = window.FooTable.init("#usertable", {
        columns: [{
          "name": "uid",
          "title": translate("us_uid"),
          "type": "text",
        },
        {
          "name": "username",
          "title": translate("us_user_name_label")
        },
        {
          "name": "acctype",
          "title": translate("us_access_type"),
          "breakpoints": "xs",
          "parser": function (value) {
            if (value === 1) {
              return translate("us_active");
            } else if (value === 99) {
              return "Admin";
            } else if (value === 0) {
              return translate("us_disabled");
            }
            return value;
          },
        },
        {
          "name": "validuntil",
          "title": translate("us_valid_until"),
          "breakpoints": "xs sm",
          "parser": function (value) {
            var comp = new Date();
            value = Math.floor(value + ((comp.getTimezoneOffset() * 60) * -1));
            var vuepoch = new Date(value * 1000);
            var formatted = vuepoch.getFullYear() +
              "-" + twoDigits(vuepoch.getMonth() + 1) +
              "-" + twoDigits(vuepoch.getDate());
            return formatted;
          },
        }
        ],
        rows: userdata,
        editing: {
          showText: "<span class=\"fooicon fooicon-pencil\" aria-hidden=\"true\"></span>&nbsp;" + translate("us_edit_users"),
          hideText: translate("g_abort"),
          addText: translate("us_new_user"),
          addRow: function () {
            $editor[0].reset();
            $editorTitle.text(translate("us_add_new_user"));
            editoropen = true;
            $modal.modal("show");
          },
          editRow: function (row) {
            var acctypefinder;
            var values = row.val();
            if (values.acctype === translate("us_active")) {
              acctypefinder = 1;
            } else if (values.acctype === "Admin") {
              acctypefinder = 99;
            } else if (values.acctype === translate("us_disabled")) {
              acctypefinder = 0;
            }
            $editor.find("#uid").val(values.uid);
            $editor.find("#username").val(values.username);
            $editor.find("#acctype").val(acctypefinder);
            $editor.find("#validuntil").val(values.validuntil);
            $modal.data("row", row);
            $editorTitle.text(translate("us_edit_user") + ": #" + values.username);
            editoropen = true;
            $modal.modal("show");
          },
          deleteRow: function (row) {
            var uid = row.value.uid;
            var username = row.value.username;
            if (confirm(translate("us_remove_user_t", uid, username))) {
              var jsontosend = "{\"uid\":\"" + uid + "\",\"command\":\"remove\"}";
              websock.send(jsontosend);
              row.delete();
            }
          }
        },
        components: {
          filtering: window.FooTable.MyFiltering
        }
      }),
      uid = 10001;
    $editor.on("submit", function (e) {
      if (this.checkValidity && !this.checkValidity()) {
        return;
      }
      e.preventDefault();
      var row = $modal.data("row"),
        values = {
          uid: $editor.find("#uid").val(),
          username: $editor.find("#username").val(),
          acctype: parseInt($editor.find("#acctype").val()),
          validuntil: (new Date($editor.find("#validuntil").val()).getTime() / 1000)
        };
      if (row instanceof FooTable.Row) {
        row.val(values);
      } else {
        values.id = uid++;
        ft.rows.add(values);
      }
      if (editoropen === true) {
        var datatosend = {};
        datatosend.command = "userfile";
        datatosend.uid = $editor.find("#uid").val();
        datatosend.user = $editor.find("#username").val();
        datatosend.acctype = parseInt($editor.find("#acctype").val());
        var validuntil = $editor.find("#validuntil").val();
        var vuepoch = (new Date(validuntil).getTime() / 1000);
        datatosend.validuntil = vuepoch;
        websock.send(JSON.stringify(datatosend));
        editoropen = false;
        $modal.modal("hide");
        loadUsers();
      }
    });
  });
}

function acctypefinder() {
  if (values.acctype === translate("us_active")) {
    return 1;
  }
  else if (values.acctype === "Admin") {
    return 99;
  }
  else {
    return 0;
  }
}

function acctypeparser() {
  var $editor = $('#editor');
  if ($editor.find('#acctype option:selected').val() == 1) {
    return translate("us_active");
  } else if ($editor.find('#acctype option:selected').val() == 99) {
    return "Admin";
  } else {
    return translate("us_disabled");
  }
}

FooTable.MyFiltering = FooTable.Filtering.extend({
  construct: function (instance) {
    this._super(instance);
    this.acctypes = ['1', '0'];
    this.acctypesstr = [translate("us_active"), translate("us_disabled")];
    this.def = translate("us_all");
    this.$acctype = null;
  },
  $create: function () {
    this._super();
    var self = this,
      $form_grp = $('<div/>', {
        'class': 'form-group'
      })
        .append($('<label/>', {
          'class': 'sr-only',
          text: 'Status'
        }))
        .prependTo(self.$form);

    self.$acctype = $('<select/>', {
      'class': 'form-control'
    })
      .on('change', {
        self: self
      }, self._onStatusDropdownChanged)
      .append($('<option/>', {
        text: self.def
      }))
      .appendTo($form_grp);

    $.each(self.acctypes, function (i, acctype) {
      self.$acctype.append($('<option/>').text(self.acctypesstr[i]).val(self.acctypes[i]));
    });
  },
  _onStatusDropdownChanged: function (e) {
    var self = e.data.self,
      selected = $(this).val();
    if (selected !== self.def) {
      self.addFilter('acctype', selected, ['acctype']);
    } else {
      self.removeFilter('acctype');
    }
    self.filter();
  },
  draw: function () {
    this._super();
    var acctype = this.find('acctype');
    if (acctype instanceof FooTable.Filter) {
      this.$acctype.val(acctype.query.val());
    } else {
      this.$acctype.val(this.def);
    }
  }
});

//Script data for Log
function loadLog() {
  if (hw_rev === "ESP8266") {
    maxLog = 50;
  }
  else {
    maxLog = 100;
  }

  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "block";
  document.getElementById("syslogContent").style.display = "none";
  clearInterval(timerRefreshStats);
  closeNav();

  var commandtosend = {};
  commandtosend.command = "latestlog";
  websock.send(JSON.stringify(commandtosend));
  commandtosend = {};
  commandtosend.command = "gettime";
  websock.send(JSON.stringify(commandtosend));
}

function initLogTable() {
  jQuery(function ($) {
    window.FooTable.init("#latestlogtable", {
      columns: [
        {
          "name": "timestamp",
          "title": translate("lo_date"),
          "parser": function (value) {
            var vuepoch = new Date(value * 1000);
            var formatted = twoDigits(vuepoch.getUTCDate())
              + "." + twoDigits(vuepoch.getUTCMonth() + 1)
              + "." + twoDigits(vuepoch.getUTCFullYear())
              + " " + twoDigits(vuepoch.getUTCHours())
              + ":" + twoDigits(vuepoch.getUTCMinutes())
              + ":" + twoDigits(vuepoch.getUTCSeconds());
            return formatted;
          },
          "sorted": true,
          "direction": "DESC"
        },
        {
          "name": "duration",
          "title": translate("lo_duration"),
          "parser": function (value) {
            if (value === 0) {
              return "<span class=\"glyphicon glyphicon-repeat\"></span>";
            }
            else if (value === "e") {
              return "<span class=\"glyphicon glyphicon-remove\"></span>";
            }
            var seconds = (value / 1000).toFixed(0);
            var minutes = Math.floor(seconds / 60);
            var hours = "";
            if (minutes > 59) {
              hours = Math.floor(minutes / 60);
              hours = (hours >= 10) ? hours : "0" + hours;
              minutes = minutes - (hours * 60);
              minutes = (minutes >= 10) ? minutes : "0" + minutes;
            }
            seconds = Math.floor(seconds % 60);
            seconds = (seconds >= 10) ? seconds : "0" + seconds;
            if (hours != "") {
              return hours + ":" + minutes + ":" + seconds;
            }
            return minutes + ":" + seconds;
          }
        },
        {
          "name": "energy",
          "title": translate("lo_energy"),
          "parser": function (value) {
            if (value === 0) {
              return "<span class=\"glyphicon glyphicon-repeat\"></span>";
            }
            else if (value === "e") {
              return "<span class=\"glyphicon glyphicon-remove\"></span>";
            }
            return value + " kWh";
          }
        },
        {
          "name": "costs",
          "title": translate("lo_costs")
        },
        {
          "name": "uid",
          "title": translate("lo_uid"),
          "breakpoints": "xs",
          "type": "text",
        },
        {
          "name": "username",
          "title": translate("lo_user"),
          "breakpoints": "xs sm",
        }
      ],
      rows: logdata
    });
  });
  logtable = true;
}

function exportLogCsv() {
  sep = document.getElementById("csvSeparator").value;
  JsonFields = ["Date", "Duration", "Energy", "Price", "Costs", "StartReading", "UID", "Username"]
  var csvStr = JsonFields.join(sep) + "\n";
  logdataraw.forEach(element => {
    var ts = new Date(element.timestamp * 1000);
    StartDate = ts.getFullYear() + '-' + (ts.getMonth() + 1) + '-' + ts.getDate() + ' ' + ts.getUTCHours() + ':' + ts.getMinutes() + ':' + ts.getSeconds();

    var dur = new Date(0);
    dur.setUTCSeconds(element.duration / 1000);
    Duration = dur.getUTCHours() + ':' + dur.getUTCMinutes() + ':' + dur.getUTCSeconds();

    if (sep === ",") {
      Energy = element.energy;
      Price = element.price;
      Costs = (element.energy * element.price / 100.0);
      if (element.hasOwnProperty('reading')) {
        StartReading = element.reading;
      }
      else {
        StartReading = 0;
      }
      UID = element.uid;
      Username = element.username;
    }
    else if (sep === ";") {
      Energy = element.energy.toString().replace(".", ",");
      Price = element.price.toString().replace(".", ",");
      Costs = (element.energy * element.price / 100.0).toString().replace(".", ",")
      if (element.hasOwnProperty('reading')) {
        StartReading = element.reading.toString().replace(".", ",");
      }
      else {
        StartReading = 0;
      }
      UID = element.uid;
      Username = element.username;
    }

    csvStr += StartDate + sep + Duration + sep + Energy + sep + Price + sep + Costs + sep + StartReading + sep + UID + sep + Username + "\n";
  })
  var hiddenElement = document.createElement('a');
  var dn = new Date();
  hiddenElement.href = 'data:text/csv;charset=utf-8,' + encodeURI(csvStr);
  hiddenElement.target = '_blank';
  hiddenElement.download = dn.getFullYear() + '' + dn.getMonth() + '' + dn.getDate() + '_' + dn.getHours() + dn.getMinutes() + dn.getSeconds() + 'evse_wifi_log.csv';
  hiddenElement.click();
}

//Script data for Syslog
function loadSyslog() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
  document.getElementById("syslogContent").style.display = "block";
  closeNav();
}

function exportSysLogJson() {
  websock.send("{\"command\":\"getsyslog\"}");
}

function appendSyslog(obj) {
  document.getElementById("syslogstream").append(obj.text);
  if (obj.text.substring(obj.text.length-1) === "\n") {
    let br = document.createElement("br");
    document.getElementById("syslogstream").append(br);
  }
}

//Script data for Settings
function loadSettings() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "block";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
  document.getElementById("syslogContent").style.display = "none";
  clearInterval(timerRefreshStats);
  clearInterval(timerRefreshEvseData);
  closeNav();

  websock.send("{\"command\":\"getconf\"}");
  handleRFID();
  handleMeter();
  handleMeterType();
  handleStaticIP();
  handleApi();
}

function updateFinished() {
  document.getElementById("h4UpdateModal").innerHTML = translate("se_rebooting");
  setTimeout(() => { 
    document.getElementById("h4UpdateModal").innerHTML = translate("se_update_finish");
    document.getElementById("bodyUpdateModal").innerHTML = translate("se_update_finish_t");
    document.getElementById("reloadUpdateModal").style.display = "block";
  }, 15000);
}

function onChangeSelectionUpdate(s) {
  fwUpdateIndex = parseInt(s.selectedOptions[0].id);
  if (language === "de") {
    document.getElementById("selectedUpdateDescription").innerHTML = jsonUpdateList.versions[fwUpdateIndex].desDE;
  }
  else {
    document.getElementById("selectedUpdateDescription").innerHTML = jsonUpdateList.versions[fwUpdateIndex].desEN;
  }
  
}

function checkFirmwareUpdate() {
  var selection = document.getElementById("updateSelectContent");
  for (var i = 0; selection.length; i++) {
    selection.remove(selection.i);
  }
  var betaversions = document.getElementById("updateShowBetaVersions").checked;
  jsonUpdateList = null;
  var url = "https://update.evse-wifi.de/update.json"
  var xhrCheck = new XMLHttpRequest();
  xhrCheck.overrideMimeType("application/json");
  xhrCheck.open('GET', url, true);
  xhrCheck.onload  = function() {
    var sel = false;
    jsonUpdateList = JSON.parse(xhrCheck.responseText);
    for (var i = 0; i < jsonUpdateList.versions.length; i++) {
      if (jsonUpdateList.versions[i].hw === hw_rev &&
          ((betaversions === false && jsonUpdateList.versions[i].beta === false) || (betaversions === true))) {
        var option = document.createElement("option");
        option.id = i;
        option.text = "Version " + jsonUpdateList.versions[i].version;
        if (sel === false) {
          option.selected = true;
          if (language === "de") {
            document.getElementById("selectedUpdateDescription").innerHTML = jsonUpdateList.versions[i].desDE;
          }
          else {
            document.getElementById("selectedUpdateDescription").innerHTML = jsonUpdateList.versions[i].desEN;
          }
          
          fwUpdateIndex = i;
          sel = true;
        }
        selection.add(option);
      }
    }
  };
  xhrCheck.send(null);
  $("#checkUpdatemodal").modal();
}

function updateSelectedFirmware() {
  var xhrGetFirmware = new XMLHttpRequest();
  xhrGetFirmware.open("GET", jsonUpdateList.versions[fwUpdateIndex].url , true);
  xhrGetFirmware.responseType = "blob";
  xhrGetFirmware.onload = function () {
    if (xhrGetFirmware.status === 200) {
      var xhrPushFirmware = new XMLHttpRequest();
      var url = "http://" + window.location.hostname + "/update"
      var formData = new FormData();
      var file = new File([xhrGetFirmware.response], "firmware.bin", { lastModified: new Date().getTime() });
      formData.append("firmware.bin", file);
      xhrPushFirmware.open("POST", url, true);
      xhrPushFirmware.send(formData);
      $("#checkUpdatemodal").modal("hide");
      showFwUpdateModal();
    }
  }
  xhrGetFirmware.send();
}

function listCONF(obj) {
  document.getElementById("configversion").innerHTML = obj.configversion;

  if(obj.hardwarerev === "ESP8266") {
    hw_rev = "ESP8266";
    document.getElementById("divRSEValue").style.display = "none";
    document.getElementById("divUseRSE").style.display = "none";
    document.getElementById("divCPInterrupt").style.display = "none";
    document.getElementById("divDisplayRotation").style.display = "none";
    document.getElementById("divDisplayOnTime").style.display = "none";
    document.getElementById("textDownloadFwVersion").innerHTML = "Download <a href=\"https://github.com/CurtRod/SimpleEVSE-WiFi/releases\" target=\"_blank\">latest version</a> from GitHub.";
  }
  else {
    document.getElementById("textDownloadFwVersion").innerHTML = "Download <a href=\"https://www.evse-wifi.de/download/\" target=\"_blank\">latest version</a>.";
  }

  //Load WiFi settings
  document.getElementById("ssid").value = obj.wifi.ssid;
  document.getElementById("wifipass").value = obj.wifi.pswd;
  document.getElementById("wifibssid").value = obj.wifi.bssid;
  document.getElementById("checkboxStaticIP").checked = obj.wifi.staticip;
  document.getElementById("ipch").value = obj.wifi.ip;
  document.getElementById("subnetch").value = obj.wifi.subnet;
  document.getElementById("gatewaych").value = obj.wifi.gateway;
  document.getElementById("dnsch").value = obj.wifi.dns;
  handleStaticIP();

  if (obj.wifi.wmode === true) {
    document.getElementById("wmodeap").checked = true;
    handleAP();
    syncBrowserTime(false);
    inApMode = true;
  }
  else {
    document.getElementById("wmodesta").checked = true;
    handleSTA();
  }

  //Load meter settings
  document.getElementById("checkboxMeter").checked = obj.meter[0].usemeter;
  document.getElementById("impkwh").value = obj.meter[0].kwhimp;
  document.getElementById("implen").value = obj.meter[0].implen;
  document.getElementById("meterphase").value = obj.meter[0].meterphase;
  document.getElementById("smetertype").value = obj.meter[0].metertype;
  document.getElementById("factor").value = obj.meter[0].factor;
  document.getElementById("price").value = obj.meter[0].price;
  handleMeterType();
  handleMeter();

  //Load RFID settings
  document.getElementById("gain").value = obj.rfid.rfidgain;
  document.getElementById("checkboxRfid").checked = obj.rfid.userfid;
  handleRFID();

  //Load NTP settings
  document.getElementById("DropDownTimezone").value = obj.ntp.timezone;
  document.getElementById("ntpIP").value = obj.ntp.ntpip;
  document.getElementById("checkboxDst").checked = obj.ntp.dst;

  //Load button settings
  document.getElementById("checkboxButtonActive").checked = obj.button[0].usebutton;

  //Load system settings
  document.getElementById("hostname").value = obj.system.hostnm;
  document.getElementById("adminpwd").value = obj.system.adminpwd;
  document.getElementById("cadminpwd").value = obj.system.adminpwd;
  document.getElementById("checkboxSafari").checked = obj.system.wsauth;
  document.getElementById("checkboxDebug").checked = obj.system.debug;
  document.getElementById("maxinstall").value = obj.system.maxinstall;
  if (obj.system.hasOwnProperty("logging")) {
    document.getElementById("checkboxEnableLogging").checked = obj.system.logging;
  }
  else {
    document.getElementById("checkboxEnableLogging").checked = true;
  }
  if (obj.system.hasOwnProperty("language")) {
    document.getElementById("language").value = obj.system.language;
  }
  else {
    document.getElementById("language").value = "en";
  }
  if (obj.system.hasOwnProperty("oledontime")) {
    document.getElementById("oledontime").value = obj.system.oledontime;
  }
  else {
    document.getElementById("oledontime").value = 120;
  }

  //document.getElementById("evsecount").value = obj.system.evsecount;  -> prep for dual evse

  //Load evse settings
  //document.getElementById("mbid").value = obj.evse[0].mbid; -> prep for dual evse
  handleOperatingMode(obj);
  if (obj.evse[0].hasOwnProperty("ledconfig")) {
    document.getElementById("ledconfig").value = obj.evse[0].ledconfig;
  }
  else if (obj.evse[0].hasOwnProperty("disableled")) {
    if (obj.evse[0].disabled === true) {
      document.getElementById("ledconfig").value = 1;
    }
    else {
      document.getElementById("ledconfig").value = 3;
    }
  }
  if (obj.evse[0].hasOwnProperty("drotation")) {
    document.getElementById("drotation").value = obj.evse[0].drotation;
  }
  if (obj.system.hasOwnProperty("api")) {
    document.getElementById("checkboxApi").checked = obj.system.api;
  }
  else {
    document.getElementById("checkboxApi").checked = true;
  }
  document.getElementById("checkboxResetCurrentAfterCharge").checked = obj.evse[0].resetcurrentaftercharge;
  //document.getElementById("lp1_install").value = obj.evse[0].maxcurrent; -> prep for dual evse
  document.getElementById("avgconsumption").value = obj.evse[0].avgconsumption;
  document.getElementById("checkboxUseRse").checked = obj.evse[0].rseactive;
  document.getElementById("rsevalue").value = obj.evse[0].rsevalue;
  handleUseRse();

  var dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(obj, null, 2));
  var dlAnchorElem = document.getElementById("downloadSet");
  dlAnchorElem.setAttribute("href", dataStr);
  dlAnchorElem.setAttribute("download", "evse-wifi-settings.json");
}

function browserTime() {
  var d = new Date(0);
  var c = new Date();
  var timestamp = Math.floor((c.getTime() / 1000) + ((c.getTimezoneOffset() * 60) * -1));
  d.setUTCSeconds(timestamp);
  document.getElementById("rtc").innerHTML = d.toUTCString().slice(0, -3);
}

function deviceTime() {
  var t = new Date(0); // The 0 there is the key, which sets the date to the epoch,
  var dst = 0;
  if (document.getElementById("checkboxDst").checked) {
    dst = 3600;
  }
  if (inApMode) {
    t.setUTCSeconds(Math.floor(utcSeconds));
    document.getElementById("DropDownTimezone").disabled = true;
    document.getElementById("checkboxDst").disabled = true;
  }
  else {
    t.setUTCSeconds(Math.floor(utcSeconds + (timezone * 3600) + dst));
    document.getElementById("DropDownTimezone").disabled = false;
    document.getElementById("checkboxDst").disabled = false;
  }
  document.getElementById("utc").innerHTML = t.toUTCString().slice(0, -3);
  document.getElementById("devicetime").innerHTML = t.toUTCString().slice(0, -3);
  utcSeconds = utcSeconds + 1;
}

function chargingTime() {
  if (evseActive === true && vehicleCharging === true) {
    chargingTime = chargingTime + 1000;
    document.getElementById("evse_charging_time").innerHTML = getTimeFormat(chargingTime);
  }
}

var t = setInterval(browserTime, 1000);
var tt = setInterval(deviceTime, 1000);
var ttt = setInterval(chargingTime, 1000);

function syncBrowserTime(reload) {
  var d = new Date();
  var timestamp = Math.floor((d.getTime() / 1000) + ((d.getTimezoneOffset() * 60) * -1));
  var datatosend = {};
  datatosend.command = "settime";
  datatosend.epoch = timestamp;
  websock.send(JSON.stringify(datatosend));
  if (reload === true) {
    location.reload();
  }
}

function handleAP() {
  document.getElementById("bssid").style.display = "none";
  document.getElementById("scanb").style.display = "none";
}

function handleSTA() {
  document.getElementById("bssid").style.display = "block";
  document.getElementById("scanb").style.display = "block";
}

function handleAPClick() {
  handleAP();
  wifiSsidBackupSTA = document.getElementById("ssid").value
  document.getElementById("ssid").value = wifiSsidBackupAP;
  wifiPassBackupSTA = document.getElementById("wifipass").value;
  document.getElementById("wifipass").value = wifiPassBackupAP;
}

function handleSTAClick() {
  handleSTA();
  wifiSsidBackupAP = document.getElementById("ssid").value
  document.getElementById("ssid").value = wifiSsidBackupSTA;
  wifiPassBackupAP = document.getElementById("wifipass").value;
  document.getElementById("wifipass").value = wifiPassBackupSTA;
}

function handleStaticIP() {
  if (document.getElementById("checkboxStaticIP").checked === true) {
    document.getElementById("divIP").style.display = "block";
    document.getElementById("divSubnet").style.display = "block";
    document.getElementById("divGateway").style.display = "block";
    document.getElementById("divDNS").style.display = "block";
  }
  else {
    document.getElementById("divIP").style.display = "none";
    document.getElementById("divSubnet").style.display = "none";
    document.getElementById("divGateway").style.display = "none";
    document.getElementById("divDNS").style.display = "none";
  }
}

function handleRFID() {
  if (document.getElementById("checkboxRfid").checked === true) {
    document.getElementById("gain").disabled = false;
  }
  else {
    document.getElementById("gain").disabled = true;
  }
}

function handleMeter() {
  if (document.getElementById("checkboxMeter").checked === true) {
    document.getElementById("impkwh").disabled = false;
    document.getElementById("implen").disabled = false;
    document.getElementById("price").disabled = false;
    document.getElementById("smetertype").disabled = false;
    document.getElementById("meterphase").disabled = false;
    handleMeterFactor();
  }
  else {
    document.getElementById("impkwh").disabled = true;
    document.getElementById("implen").disabled = true;
    document.getElementById("price").disabled = true;
    document.getElementById("smetertype").disabled = true;
    document.getElementById("meterphase").disabled = true;
    document.getElementById("factor").disabled = true;
  }
}

function handleMeterFactor() {
  if (document.getElementById("meterphase").value === "3" ||
      document.getElementById("smetertype").value === "SDM630") {
    document.getElementById("factor").value = "1";
    document.getElementById("factor").disabled = true;;
  }
  else {
    document.getElementById("factor").disabled = false;
  }
}

function handleMeterType() {
  if (document.getElementById("smetertype").value !== "S0") {
    document.getElementById("divImpKwh").style.display = "none";
    document.getElementById("divImpLen").style.display = "none";
    document.getElementById("divMeterPhase").style.display = "none";
    document.getElementById("meterRegisters").style.display = "block";
  }
  else {
    document.getElementById("divImpKwh").style.display = "block";
    document.getElementById("divMeterPhase").style.display = "block";
    document.getElementById("divImpLen").style.display = "block";
    document.getElementById("meterRegisters").style.display = "none";
    if (document.getElementById("smetertype").value === "SDM120") {
      document.getElementById("meterphase").value = "1";
    }
  }
  handleMeterFactor();
}

function handleUseRse() {
  if (document.getElementById("checkboxUseRse").checked === true) {
    document.getElementById("rsevalue").disabled = false;
  }
  else {
    document.getElementById("rsevalue").disabled = true;
  }
}

function handleOperatingMode(obj) {
  if (obj.evse[0].remote === true) {
    document.getElementById("radioOperatingMode_Remote").checked = true;
  }
  else if (obj.evse[0].alwaysactive === true) {
    document.getElementById("radioOperatingMode_AlwaysActive").checked = true;
  }
  else {
    document.getElementById("radioOperatingMode_Normal").checked = true;
  }
}

function handleApi() {
  if (document.getElementById("radioOperatingMode_Remote").checked === true) {
    document.getElementById("checkboxApi").disabled = true;
    document.getElementById("checkboxApi").checked = true;
    document.getElementById("checkboxResetCurrentAfterCharge").disabled = true;
    document.getElementById("checkboxResetCurrentAfterCharge").checked = false;
  }
  if (document.getElementById("radioOperatingMode_AlwaysActive").checked === true) {
    document.getElementById("checkboxApi").disabled = false;
    document.getElementById("checkboxApi").checked = false;
    document.getElementById("checkboxResetCurrentAfterCharge").disabled = false;
    document.getElementById("checkboxResetCurrentAfterCharge").checked = true;
  }
  if (document.getElementById("radioOperatingMode_Normal").checked === true) {
    document.getElementById("checkboxApi").disabled = false;
    document.getElementById("checkboxApi").checked = false;
    document.getElementById("checkboxResetCurrentAfterCharge").disabled = false;
    document.getElementById("checkboxResetCurrentAfterCharge").checked = true;
  }
}

function shWifi() {
  var x = document.getElementById("wifipass");
  if (x.type === "password") {
    x.type = "text";
    document.getElementById("shwifi").innerHTML = translate("g_hide");
  } else {
    x.type = "password";
    document.getElementById("shwifi").innerHTML = translate("g_show");
  }
}
function shAdmin() {
  var x = document.getElementById("adminpwd");
  if (x.value != "") {
    if (x.type === "password") {
      x.type = "text";
      document.getElementById("shadmin").innerHTML = translate("g_hide");
    } else {
      x.type = "password";
      document.getElementById("shadmin").innerHTML = translate("g_show");
    }
  }
}

function interruptCp() {
  websock.send("{\"command\":\"interruptcp\"}");
}

function listSSID(obj) {
  obj.list.sort(function (a, b) { return a.rssi <= b.rssi });
  var select = document.getElementById("ssidList");
  for (var i = 0; i < obj.list.length; i++) {
    var opt = document.createElement("option");
    opt.value = obj.list[i].ssid;
    opt.bssidvalue = obj.list[i].bssid;
    opt.innerHTML = obj.list[i].ssid + " (BSSID: " + obj.list[i].bssid + ", Signal: " + obj.list[i].rssi + "dBm)";
    select.appendChild(opt);
  }
  document.getElementById("scanb").innerHTML = "Re-Scan";
  listBSSID();
}

function listBSSID(obj) {
  var select = document.getElementById("ssidList");
  document.getElementById("wifibssid").value = select.options[select.selectedIndex].bssidvalue;
}

function scanWifi() {
  websock.send("{\"command\":\"scan\"}");
  document.getElementById("scanb").innerHTML = "...";
  document.getElementById("ssid").style.display = "none";
  var node = document.getElementById("ssidList");
  node.style.display = "inline";
  while (node.hasChildNodes()) {
    node.removeChild(node.lastChild);
  }
}

function saveConf() {
  //Validate input
  var a = document.getElementById("adminpwd").value;
  if (a === null || a === "") {
    alert(translate("se_alert_admin_pw_empty"));
    return;
  }
  else if (a.length < 8) {
    alert(translate("se_alert_admin_pw_char"));
    return;
  }
  else if (a !== document.getElementById("cadminpwd").value) {
    alert(translate("se_alert_admin_pw_match"));
    return;
  }

  var ssid;
  if (document.getElementById("ssid").style.display === "none") {
    var b = document.getElementById("ssidList");
    ssid = b.options[b.selectedIndex].value;
  } else {
    ssid = document.getElementById("ssid").value;
  }
  var datatosend = {};
  datatosend.command = "configfile";
  datatosend.configversion = 1;
  datatosend.wifi = {};
  datatosend.meter = [];
  datatosend.meter[0] = {};
  datatosend.rfid = {};
  datatosend.ntp = {};
  datatosend.button = [];
  datatosend.button[0] = {};
  datatosend.system = {};
  datatosend.evse = [];
  datatosend.evse[0] = {};

  var wmode = false;
  if (document.getElementById("wmodeap").checked) {
    wmode = true;
    datatosend.wifi.bssid = document.getElementById("wifibssid").value = "";
    if (document.getElementById("wifipass").value.length < 8 &&
      document.getElementById("wifipass").value.length !== 0) {
      alert(translate("se_alert_wifi_pw_char"));
      return;
    }
  }
  else {
    datatosend.wifi.bssid = document.getElementById("wifibssid").value;
  }

  datatosend.wifi.ssid = ssid;
  datatosend.wifi.wmode = wmode;
  datatosend.wifi.pswd = document.getElementById("wifipass").value;

  if (document.getElementById("checkboxStaticIP").checked === true) {
    datatosend.wifi.staticip = document.getElementById("checkboxStaticIP").checked;
    datatosend.wifi.ip = document.getElementById("ipch").value;
    datatosend.wifi.subnet = document.getElementById("subnetch").value;
    datatosend.wifi.gateway = document.getElementById("gatewaych").value;
    datatosend.wifi.dns = document.getElementById("dnsch").value;
  }
  else {
    datatosend.wifi.staticip = false;
    datatosend.wifi.ip = "";
    datatosend.wifi.subnet = "";
    datatosend.wifi.gateway = "";
    datatosend.wifi.dns = "";
  }

  datatosend.meter[0].usemeter = document.getElementById("checkboxMeter").checked;
  datatosend.meter[0].metertype = document.getElementById("smetertype").value;
  datatosend.meter[0].price = parseFloat(document.getElementById("price").value);
  //datatosend.meter[0].intpin = parseInt(document.getElementById("gpioint").value);
  datatosend.meter[0].kwhimp = parseInt(document.getElementById("impkwh").value);
  datatosend.meter[0].implen = parseInt(document.getElementById("implen").value);
  datatosend.meter[0].meterphase = parseInt(document.getElementById("meterphase").value);
  datatosend.meter[0].factor = parseInt(document.getElementById("factor").value);

  datatosend.rfid.userfid = document.getElementById("checkboxRfid").checked;
  //datatosend.rfid.sspin = parseInt(document.getElementById("gpioss").value);
  datatosend.rfid.rfidgain = parseInt(document.getElementById("gain").value);

  datatosend.ntp.ntpip = document.getElementById("ntpIP").value;
  datatosend.ntp.timezone = parseInt(document.getElementById("DropDownTimezone").value);
  datatosend.ntp.dst = document.getElementById("checkboxDst").checked;

  datatosend.button[0].usebutton = document.getElementById("checkboxButtonActive").checked;
  //datatosend.button[0].buttonpin = parseInt(document.getElementById("gpiobutton").value);

  datatosend.system.hostnm = document.getElementById("hostname").value;
  datatosend.system.adminpwd = a;
  datatosend.system.wsauth = document.getElementById("checkboxSafari").checked;
  datatosend.system.debug = document.getElementById("checkboxDebug").checked;
  datatosend.system.maxinstall = parseInt(document.getElementById("maxinstall").value);
  datatosend.system.logging = document.getElementById("checkboxEnableLogging").checked;
  datatosend.system.api = document.getElementById("checkboxApi").checked;
  datatosend.system.evsecount = 1;
  datatosend.system.language = document.getElementById("language").value;
  datatosend.system.oledontime = document.getElementById("oledontime").value;

  datatosend.evse[0].mbid = 1;
  if (document.getElementById("radioOperatingMode_Remote").checked === true) {
    datatosend.evse[0].alwaysactive = true;
    datatosend.evse[0].remote = true;
  } else {
    datatosend.evse[0].alwaysactive = document.getElementById("radioOperatingMode_AlwaysActive").checked;
    datatosend.evse[0].remote = false;
  }
  datatosend.evse[0].ledconfig = parseInt(document.getElementById("ledconfig").value);
  datatosend.evse[0].drotation = parseInt(document.getElementById("drotation").value);
  datatosend.evse[0].resetcurrentaftercharge = document.getElementById("checkboxResetCurrentAfterCharge").checked;
  datatosend.evse[0].evseinstall = parseInt(document.getElementById("maxinstall").value);
  datatosend.evse[0].avgconsumption = parseFloat(document.getElementById("avgconsumption").value);
  if (hw_rev === "ESP8266") {
    datatosend.evse[0].rseactive = false;
    datatosend.evse[0].rsevalue = 0;
  }
  else {
    datatosend.evse[0].rseactive = document.getElementById("checkboxUseRse").checked;
    datatosend.evse[0].rsevalue = parseInt(document.getElementById("rsevalue").value);
  }
  
  websock.send(JSON.stringify(datatosend));
  alert(translate("se_alert_reboot"));
  location.reload();
}

function backupuser() {
  var commandtosend = {};
  commandtosend.command = "userlist";
  commandtosend.page = page;
  websock.send(JSON.stringify(commandtosend));
}

function backupset() {
  var dlAnchorElem = document.getElementById("downloadSet");
  dlAnchorElem.click();
}

function piccBackup(obj) {
  var dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(obj, null, 2));
  var dlAnchorElem = document.getElementById("downloadUser");
  dlAnchorElem.setAttribute("href", dataStr);
  dlAnchorElem.setAttribute("download", "evse-wifi-users.json");
  dlAnchorElem.click();
}

function restoreSet() {
  var input = document.getElementById("restoreSet");
  var reader = new FileReader();
  if ("files" in input) {
    if (input.files.length === 0) {
      alert(translate("se_alert_no_file_restore"));
    } else {
      reader.onload = function () {
        var json;
        try {
          json = JSON.parse(reader.result);
        } catch (e) {
          alert(translate("se_alert_no_valid_backup"));
          return;
        }
        if (!json.hasOwnProperty("command")) {
          json.command = "configfile";
        }
        if (json.hasOwnProperty("configversion")) {
          var x = confirm(translate("se_confirm_backup"));
          if (x) {
            websock.send(JSON.stringify(json));
            alert(translate("se_alert_reboot"));
            location.reload();
          }
        }
        else {
          alert(translate("se_alert_no_valid_backup"));
          return;
        }
      };
      reader.readAsText(input.files[0]);
    }
  }
}

function restore1by1(i, len, data) {
  var part = 100 / len;
  var uid, user, acc, valid;
  document.getElementById("dynamic").style.width = part * (i + 1) + "%";
  var datatosend = {};
  uid = data[i].uid;
  user = data[i].username;
  acc = data[i].acctype;
  valid = data[i].validuntil;
  datatosend.command = "userfile";
  datatosend.uid = uid;
  datatosend.user = user;
  datatosend.acctype = acc;
  datatosend.validuntil = valid;
  websock.send(JSON.stringify(datatosend));
  slot++;
  if (slot === len) {
    document.getElementById("dynamic").className = "progress-bar progress-bar-success";
    document.getElementById("dynamic").innerHTML = "Completed";
    document.getElementById("dynamic").style.width = "100%";
    completed = true;
    document.getElementById("restoreclose").style.display = "block";
  }
}

function restoreUser() {
  var input = document.getElementById("restoreUser");
  var reader = new FileReader();
  if ("files" in input) {
    if (input.files.length === 0) {
      alert(translate("se_alert_no_file_restore"));
    } else {
      reader.onload = function () {
        var json;
        try {
          json = JSON.parse(reader.result);
        } catch (e) {
          alert(translate("se_alert_no_valid_backup"));
          return;
        }
        if (json.type === "evse-wifi-userbackup") {
          var x = confirm(translate("se_confirm_backup"));
          if (x) {
            recordstorestore = json.list.length;
            userdata = json.list;
            $("#restoremodal").modal();
            restore1by1(slot, recordstorestore, userdata);
          }
        }
        else {
          alert(translate("se_alert_no_valid_backup"));
          return;
        }
      };
      reader.readAsText(input.files[0]);
    }
  }
}

function resetLogFile() {
  if (confirm(translate("se_confirm_rst_log"))) {
    websock.send("{\"command\":\"initlog\"}");
    alert(translate("se_alert_logfile_reset"));
    location.reload();
  }
  else {
    alert(translate("se_alert_aborted"));
  }
}

function resetUserData() {
  if (confirm(translate("se_confirm_rst_user"))) {
    websock.send("{\"command\":\"resetuserdata\"}");
    alert(translate("se_alert_user_reset"));
    location.reload();
  }
  else {
    alert(translate("se_alert_aborted"));
  }
}

function resetFactoryReset() {
  if (confirm(translate("se_confirm_rst_factory"))) {
    websock.send("{\"command\":\"factoryreset\"}");
    alert(translate("se_alert_factory_restored"));
    location.reload();
  }
  else {
    alert(translate("se_alert_aborted"));
  }
}

function showFwUpdateModal() {
  $("#updatemodal").modal();
}

function colorStatusbar(ref) {
  var percentage = ref.style.width.slice(0, -1);
  if (percentage > 50) ref.className = "progress-bar progress-bar-success";
  else if (percentage > 25) ref.className = "progress-bar progress-bar-warning";
  else ref.class = "progress-bar progress-bar-danger";
}

//Script data for Timer
function loadTimer() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "block";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
  clearInterval(timerRefreshStats);
  clearInterval(timerRefreshEvseData);
  closeNav();
  websock.send("{\"command\":\"getevsetimer\"}");
}

function listTimer(obj) {
  if (obj.hasOwnProperty("active")) {
    document.getElementById("timer_active").checked = obj.active;
  }
  else {
    document.getElementById("timer_active").checked = true;
  }
  handleTimerActivate();
  for (var i = 0; i < obj.list.length; i++) {
    var j = i + 1;
    var secsFrom = obj.list[i].from;
    var fromH = Math.floor(secsFrom / 3600);
    secsFrom %= 3600;
    var fromM = Math.floor(secsFrom / 60);
  
    var secsTo = obj.list[i].to;
    var toH = Math.floor(secsTo / 3600);
    secsTo %= 3600;
    var toM = Math.floor(secsTo / 60);

    document.getElementById("timer" + j + "_active").checked = obj.list[i].active;
    document.getElementById("timer" + j + "_fromH").value = fromH;
    document.getElementById("timer" + j + "_fromM").value = fromM;
    document.getElementById("timer" + j + "_toH").value = toH;
    document.getElementById("timer" + j + "_toM").value = toM;
    if (obj.list[i].hasOwnProperty("current")) {
      if (obj.list[i].current > 64) {
        document.getElementById("timer" + j + "_current").value = obj.list[i].current / 100.0;
      }
      else {
        document.getElementById("timer" + j + "_current").value = obj.list[i].current;
      }
    }
    else {
      document.getElementById("timer" + j + "_current").value = null;
    }

    if (obj.list[i].days.includes(2)) { //Monday
      document.getElementById("timer" + j + "_mo").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_mo").checked = false;
    }
    if (obj.list[i].days.includes(3)) { //Tuesday
      document.getElementById("timer" + j + "_tu").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_tu").checked = false;
    }
    if (obj.list[i].days.includes(4)) { //Wednesday
      document.getElementById("timer" + j + "_we").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_we").checked = false;
    }
    if (obj.list[i].days.includes(5)) { //Thursday
      document.getElementById("timer" + j + "_th").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_th").checked = false;
    }
    if (obj.list[i].days.includes(6)) { //Friday
      document.getElementById("timer" + j + "_fr").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_fr").checked = false;
    }
    if (obj.list[i].days.includes(7)) { //Saturday
      document.getElementById("timer" + j + "_sa").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_sa").checked = false;
    }
    if (obj.list[i].days.includes(1)) { //Sunday
      document.getElementById("timer" + j + "_su").checked = true;
    }
    else {
      document.getElementById("timer" + j + "_su").checked = false;
    }
  }
}

function saveTimer() {
  var datatosend = {};
  datatosend.command = "timer";
  datatosend.list = [];
  
  datatosend.active = document.getElementById("timer_active").checked;

  for (var i = 0; i < 5; i++) {
    var j = i + 1;
    var days = "";
    if (document.getElementById("timer" + j + "_su").checked === true) {
      days += "1";
    }
    if (document.getElementById("timer" + j + "_mo").checked === true) {
      days += "2";
    }
    if (document.getElementById("timer" + j + "_tu").checked === true) {
      days += "3";
    }
    if (document.getElementById("timer" + j + "_we").checked === true) {
      days += "4";
    }
    if (document.getElementById("timer" + j + "_th").checked === true) {
      days += "5";
    }
    if (document.getElementById("timer" + j + "_fr").checked === true) {
      days += "6";
    }
    if (document.getElementById("timer" + j + "_sa").checked === true) {
      days += "7";
    }

    var eActive = document.getElementById("timer" + j + "_active").checked;
    var eFrom = (document.getElementById("timer" + j + "_fromH").value * 3600 + document.getElementById("timer" + j + "_fromM").value * 60);
    var eTo = (document.getElementById("timer" + j + "_toH").value * 3600 + document.getElementById("timer" + j + "_toM").value * 60);
    if (highResolution) {
      var eCurrent = parseFloat(document.getElementById("timer" + j + "_current").value) * 100.0;
    }
    else {
      var eCurrent = parseInt(document.getElementById("timer" + j + "_current").value);
    }
    if (eCurrent < 6) {
      eCurrent = 0;
    }
    
    var eDays = days;

    datatosend.list.push({
      "active" : eActive,
      "from"   : eFrom,
      "to"     : eTo,
      "days"   : eDays,
      "current": eCurrent
    })
  }
  websock.send(JSON.stringify(datatosend));
  alert(translate("ti_saved"));
  loadTimer();
}

function handleTimerActivate() {
  var tf = true;
  if (document.getElementById("timer_active").checked) {
    tf = false;
  }
  for (var i = 1; i < 6; i++) {
    document.getElementById("timer" + i + "_active").disabled = tf;
    document.getElementById("timer" + i + "_fromH").disabled = tf;
    document.getElementById("timer" + i + "_fromM").disabled = tf;
    document.getElementById("timer" + i + "_toH").disabled = tf;
    document.getElementById("timer" + i + "_toM").disabled = tf;
    document.getElementById("timer" + i + "_mo").disabled = tf;
    document.getElementById("timer" + i + "_tu").disabled = tf;
    document.getElementById("timer" + i + "_we").disabled = tf;
    document.getElementById("timer" + i + "_th").disabled = tf;
    document.getElementById("timer" + i + "_fr").disabled = tf;
    document.getElementById("timer" + i + "_sa").disabled = tf;
    document.getElementById("timer" + i + "_su").disabled = tf;
    document.getElementById("timer" + i + "_current").disabled = tf;
  }
}

//Script data for Status
function loadStatus() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "block";
  document.getElementById("logContent").style.display = "none";
  document.getElementById("syslogContent").style.display = "none";
  clearInterval(timerRefreshStats);
  clearInterval(timerRefreshEvseData);
  closeNav();
  
  refreshStats();
  timerRefreshStats = setInterval(refreshStatsCountdown, 1000);
  refreshSeconds = 10;

  websock.send("{\"command\":\"getconf\"}");
  handleRFID();
  handleMeter();
  handleMeterType();
  handleStaticIP();
  handleApi();
}

function refreshStats() {
  websock.send("{\"command\":\"status\"}");
}

function refreshStatsCountdown() {
  document.getElementById("st_head").innerHTML = translate("st_head", refreshSeconds);
  if (refreshSeconds === 0) {
    refreshStats();
    refreshSeconds = 10;
  }
  else {
    refreshSeconds -= 1;
  }
}

function listStats(obj) {
  document.getElementById("chip").innerHTML = obj.chipid;
  document.getElementById("cpu").innerHTML = obj.cpu + " Mhz";
  document.getElementById("uptime").innerHTML = obj.uptime;
  document.getElementById("heap").innerHTML = Math.round((obj.heap / 1024) * 10) / 10 + " kB";
  if (obj.hardwarerev === "ESP8266") {
    document.getElementById("heap").style.width = (obj.heap * 100) / 81920 + "%";
    //document.getElementById("divintTemp").style.display = "none";
  }
  else {
    document.getElementById("heap").style.width = (obj.heap * 100) / 327680 + "%";
    //document.getElementById("int_temp").innerHTML = obj.int_temp + " °C";
  }
  colorStatusbar(document.getElementById("heap"));
  document.getElementById("flash").innerHTML = Math.round((obj.availsize / 1024) * 10) / 10 + " kB";
  //document.getElementById("flash").style.width = (obj.availsize * 100) / (4194304 - obj.spiffssize) + "%";
  document.getElementById("flash").style.width = (obj.availsize * 100) / (obj.flashsize - obj.spiffssize) + "%";
  colorStatusbar(document.getElementById("flash"));
  document.getElementById("spiffs").innerHTML = Math.round((obj.availspiffs / 1024) * 10) / 10 + " kB";
  document.getElementById("spiffs").style.width = (obj.availspiffs * 100) / obj.spiffssize + "%";
  colorStatusbar(document.getElementById("spiffs"));
  document.getElementById("ssidstat").innerHTML = obj.ssid + "<object id=\"rssi\"></object>";
  document.getElementById("ip").innerHTML = obj.ip;
  document.getElementById("gate").innerHTML = obj.gateway;
  document.getElementById("mask").innerHTML = obj.netmask;
  document.getElementById("dns").innerHTML = obj.dns;
  document.getElementById("mac").innerHTML = obj.mac;
  document.getElementById("host").innerHTML = obj.hostname;
  document.getElementById("amps_conf").innerHTML = obj.evse_amps_conf;			//1000
  document.getElementById("amps_out").innerHTML = obj.evse_amps_out;			//1001
  document.getElementById("vehicle_status").innerHTML = obj.evse_vehicle_state;	//1002
  document.getElementById("amps_pp").innerHTML = obj.evse_pp_limit;				//1003
  document.getElementById("amps_turn_off").innerHTML = obj.evse_turn_off;		//1004
  document.getElementById("evse_version").innerHTML = obj.evse_firmware;		//1005
  document.getElementById("evse_status").innerHTML = obj.evse_state;			//1006
  //document.getElementById("evse_rcd").innerHTML = obj.evse_rcd;			//1007
  document.getElementById("amps_boot").innerHTML = obj.evse_amps_afterboot;		//2000
  document.getElementById("evse_modbus").innerHTML = obj.evse_modbus_enabled;	//2001
  document.getElementById("amps_min").innerHTML = obj.evse_amps_min;			//2002
  document.getElementById("evse_analog_in").innerHTML = obj.evse_analog_input;	//2003
  document.getElementById("amps_power_on").innerHTML = obj.evse_amps_poweron;	//2004
  document.getElementById("evse_2005").innerHTML = obj.evse_2005;				//2005
  document.getElementById("evse_sharing_mode").innerHTML = obj.evse_sharing_mode;//2006
  document.getElementById("pp_detection").innerHTML = obj.evse_pp_detection;	//2007
  document.getElementById("meter_total").innerHTML = obj.meter_total;
  document.getElementById("meter_p1").innerHTML = obj.meter_p1;
  document.getElementById("meter_p2").innerHTML = obj.meter_p2;
  document.getElementById("meter_p3").innerHTML = obj.meter_p3;
  document.getElementById("meter_v1").innerHTML = obj.meter_p1_v;
  document.getElementById("meter_v2").innerHTML = obj.meter_p2_v;
  document.getElementById("meter_v3").innerHTML = obj.meter_p3_v;
  if (obj.hasOwnProperty("rssi")) {
    document.getElementById("rssi").innerHTML = " (" + obj.rssi + "dBm)";
    document.getElementById("rssi").style.fontWeight = 'bold';
    if (obj.rssi < -84) { // bad wifi signal
      document.getElementById("rssi").style.color = '#c30';
    }
    else if (obj.rssi < -69) { // middle-bad wifi signal
      document.getElementById("rssi").style.color = '#fc0';
    }
    else if (obj.rssi < -59) { // middle-good wifi signal
      document.getElementById("rssi").style.color = '#6f3';
    }
    else { // good wifi signal
      document.getElementById("rssi").style.color = '#093';
    }
  }
  else {
    document.getElementById("rssi").innerHTML = " (AP Mode)";
    document.getElementById("rssi").style.color = '#093';
  }
}

function setEVSERegister() {
  var datatosend = {};
  datatosend.command = "setevsereg";
  datatosend.register = document.getElementById("evseRegToSet").value;
  datatosend.value = document.getElementById("evseRegValue").value;
  websock.send(JSON.stringify(datatosend));
  document.getElementById("buttonsetregister").disabled = true;
  $("#loadersetevsereg").removeClass('hidden');
  
  setTimeout(function () {
    refreshStats();
    $("#evseRegModal").modal("hide");
    document.getElementById("buttonsetregister").disabled = false;
    $("#loadersetevsereg").addClass('hidden');
  }, 3000);
}

//General functions
function round(x) {
  var k = (Math.round(x * 100) / 100).toString();
  k += (k.indexOf('.') == -1) ? '.00' : '00';
  return k.substring(0, k.indexOf('.') + 3);
}

function twoDigits(value) {
  if (value < 10) {
    return "0" + value;
  }
  return value;
}

function openNav() {
  document.getElementById("mySidenav").style.width = "250px";
}

function closeNav() {
  document.getElementById("mySidenav").style.width = "0";
}

function showCurrentModal() {
  $("#currentModal").modal();
  currentModalOpen = true;
}

function showInfoModal() {
  $("#infoModal").modal();
}

function showEvseRegModal() {
  $("#evseRegModal").modal();
}

$(document).ready(function () {
  //$('[data-toggle="tooltip"]').tooltip();
});

function socketMessageListener(evt) {
  var obj = JSON.parse(evt.data);
  if (obj.command === "getevsedata") {
    listEVSEData(obj);
  }
  else if (obj.command === "piccscan") {
    if (document.getElementById("usersContent").style.display === "block") {
      listSCAN(obj);
    }
  }
  else if (obj.command === "gettime") {
    timezone = obj.timezone;
    utcSeconds = obj.epoch;
  }
  else if (obj.command === "userlist") {
    haspages = obj.haspages;
    if (haspages === 0) {
      document.getElementById("usertable").innerHTML = null;
      initUserTable();
      $(".footable-show").click();
      $(".fooicon-remove").click();
    }
    builduserdata(obj);
  }
  else if (obj.command === "result") {
    if (obj.resultof === "userfile" && document.getElementById("settingsContent").style.display !== "none") {
      if (!completed && obj.result === true) {
        restore1by1(slot, recordstorestore, userdata);
      }
    }
    else if (obj.resultof === "userlist") {
      if (page < haspages && obj.result === true) {
        getnextpage(page);
      }
      else if (page === haspages) {
        if (document.getElementById("settingsContent").style.display !== "none") {
          file.type = "evse-wifi-userbackup";
          file.version = "v0.1";
          file.list = userdata;
          piccBackup(file);
        }
        else if (document.getElementById("usersContent").style.display !== "none") {
          document.getElementById("usertable").innerHTML = null;
          initUserTable();
          $(".footable-show").click();
          $(".fooicon-remove").click();
          websock.send("{\"command\":\"gettime\"}");
        }
      }
    }
  }
  else if (obj.command === "ssidlist") {
    listSSID(obj);
  }
  else if (obj.command === "startupinfo") {
    hw_rev = obj.hw_rev;
    sw_rev = obj.sw_rev;
    pp_limit = obj.pp_limit;
      $.getScript("lang.js", function() {
        $(document).ready(function() {});
     });
    if (obj.language === "de") {
      langMap = lang_de;
      language = "de";
    }
    else {
      langMap = lang_en;
      language = "en";
    }
    translateAll();
    opmode = obj.opmode;
    highResolution = obj.highResolution;

    if (highResolution) {
      document.getElementById("currentSlider").setAttribute("step", "0.5"); //Test -> 0,1
      document.getElementById("timer1_current").setAttribute("step", "0.5");//Test -> 0,1
      document.getElementById("timer2_current").setAttribute("step", "0.5");//Test -> 0,1
      document.getElementById("timer3_current").setAttribute("step", "0.5");//Test -> 0,1
      document.getElementById("timer4_current").setAttribute("step", "0.5");//Test -> 0,1
      document.getElementById("timer5_current").setAttribute("step", "0.5");//Test -> 0,1
    }
    
    const fw = document.getElementsByClassName("fw_version");
    for (let i of fw) { i.innerHTML = sw_rev; }
    if (opmode === 0) {
      document.getElementById("timerLink").style.display = "block";
    }
    else {
      document.getElementById("timerLink").style.display = "none";
    }
    if (obj.debug === true) {
      document.getElementById("syslogLink").style.display = "block";
    }
    else {
      document.getElementById("syslogLink").style.display = "none";
    }
  }
  else if (typeof obj.configversion !== "undefined") {
    listCONF(obj);
    websock.send("{\"command\":\"gettime\"}");
  }
  else if (obj.command === "userlist") {
    haspages = obj.haspages;
    builduserdata(obj);
  }
  else if (obj.command === "status") {
    listStats(obj);
  }
  else if (obj.command === "timer") {
    listTimer(obj);
  }
  else if (obj.command === "updateFinished") {
    updateFinished();
  }
  else if (obj.command === "syslog") {
    appendSyslog(obj);
  }
  if (obj.type === "latestlog") {
    logdataraw = obj.list
    logdata = obj.list;
    for (i in logdata) {
      if (logdata[i].price === "e") {
        logdata[i].costs = "<span class=\"glyphicon glyphicon-remove\"></span>";
      }
      else {
        logdata[i].costs = String(round(logdata[i].price / 100 * logdata[i].energy)) + " €";
      }
    }
    if (logdata.length > 0 || logtable === false) {
      document.getElementById("latestlogtable").innerHTML = null;
      initLogTable();
    }
    if (logdata.length > 0) {
      document.getElementById("textlimitlogfile").innerHTML = translate("lo_head", maxLog, logdata.length);
    }
  }
  if (obj.hasOwnProperty("syslog_export")) {
    var dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(obj, null, 2));
    var downloadAnchorNode = document.createElement('a');
    downloadAnchorNode.setAttribute("href",     dataStr);
    downloadAnchorNode.setAttribute("download", "syslog_export.json");
    document.body.appendChild(downloadAnchorNode); // required for firefox
    downloadAnchorNode.click();
    downloadAnchorNode.remove();
  }
}

function socketCloseListener(evt) {
  console.log('socket closed');
  websock = new WebSocket(wsUri);
  websock.addEventListener('message', socketMessageListener);
  websock.addEventListener('close', socketCloseListener);
  websock.addEventListener('error', socketErrorListener);
}

function socketErrorListener(evt) {
  console.log('socket error');
  console.log(evt);
}
function login() {
  var username = "admin"
  var passwd = document.getElementById("password").value;
  var url = "/login";
  var xhr = new XMLHttpRequest();
  xhr.open("get", url, true, username, passwd);
  xhr.onload = function (e) {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        wsConnect();
      } else {
        alert("Incorrect password!");
      }
    }
  };
  xhr.send(null);
}

function wsConnect() {
  var protocol = "ws://";
  if (window.location.protocol === "https:") {
    protocol = "wss://";
  }

  wsUri = protocol + window.location.hostname + "/ws";
  websock = new WebSocket(wsUri);
  websock.addEventListener('message', socketMessageListener);
  websock.addEventListener('error', socketErrorListener);
  websock.addEventListener('close', socketCloseListener);

  websock.onopen = function (evt) {
    websock.send("{\"command\":\"getstartup\"}");
    loadEVSEControl();
  };
  setTimeout(function() {
    timeoutInterval = setInterval(connectionTimeOutCheck, 3000);
  }, 2000); 
}

function connectionTimeOutCheck() {
  var readyState = websock.readyState;
  if (readyState !== 1) {
    clearInterval(timeoutInterval);
    if (confirm(translate("g_confirm_conn_lost"))) {
      location.reload();
    }
    else {
      timeoutInterval = setInterval(connectionTimeOutCheck(), 3000);
    }
  }
}

function start() {
  document.getElementById("evseContent").style.display = "none";
  document.getElementById("usersContent").style.display = "none";
  document.getElementById("settingsContent").style.display = "none";
  document.getElementById("timerContent").style.display = "none";
  document.getElementById("statusContent").style.display = "none";
  document.getElementById("logContent").style.display = "none";
}


function translate(val, var0 = null, var1 = null, var2 = null) {
  var retValue = langMap[val];
  if (var0 != null) {
      retValue = retValue.replace("{0}", var0);
  }
  if (var1 != null) {
      retValue = retValue.replace("{1}", var1);
  }
  if (var2 != null) {
      retValue = retValue.replace("{2}", var2);
  }
  return retValue;
}

function translateAll() {
  for (const [key, val] of Object.entries(langMap)) {
    const objects = document.getElementsByClassName(key);
    for (let i of objects) { i.innerHTML = val; }
    const object = document.getElementById(key);
    if (object != null) {object.title = val; }
  }
  $('[data-toggle="tooltip"]').tooltip();
}