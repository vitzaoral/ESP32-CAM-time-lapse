<?php
header('Content-Type: application/json');

$received = file_get_contents('php://input');
$file_to_write = date("d.m.Y-H:i:s") . ".jpg";

$camera_number = $_GET['camera'];

// TODO: insert your settings !
$cloudinary_upload_preset = "TODO";
$cloudinary_url = "https://api.cloudinary.com/v1_1/vitaz/image/upload";

$blynk_auth_token_bees = "TODO"; // TODO: blynk2 token..

$blynk_auth_token_cam1 = "TODO";
$blynk_auth_token_cam2 = "TODO";
$blynk_auth_token_cam3 = "TODO";

// get outside temperature
$url = "http://blynk-cloud.com/" . $blynk_auth_token_bees . "/get/V23"; // TODO: blynk2 ..

$ch = curl_init();
$options = array(
    CURLOPT_URL => $url,
    CURLOPT_HEADER => false,
    CURLOPT_RETURNTRANSFER => true
);

curl_setopt_array($ch, $options);
$response = curl_exec($ch);
curl_close($ch);

$temperature = str_replace("[\"", '', $response);
$temperature = str_replace("0\"]", '', $temperature);

// Insert text to image
$jpg_image = imagecreatefromstring($received);
$text = "CAM" . $camera_number . " " . date("d.m.Y H:i:s") . " " . $temperature . "°C";
$font =  dirname(__FILE__) . '/arial.ttf';
$font_color = imagecolorallocate($jpg_image, 0, 0, 0);
$bg_color = imagecolorallocatealpha($jpg_image, 220, 220, 220, 100);

$font_size = 40;
$offset_x = 0;
$offset_y = 85;
$text_width = 800;
$text_height = 55;
$text_rectangle_offset = 2;

imagefilledrectangle($jpg_image, $offset_x, $offset_y + $text_rectangle_offset, $offset_x + $text_width, $offset_y - $text_height, $bg_color);
imagettftext($jpg_image, $font_size, 0, $offset_x, $offset_y - 7, $font_color, $font, $text);

// Quality from 0 (LOW) to 100 (BEST)
$jpg_quality = 90;
imagejpeg($jpg_image, $file_to_write, $jpg_quality);
imagedestroy($jpg_image);

// Send image to Cloudinary API
$headers = array("Content-Type:multipart/form-data");
$folder = "camera_" . $camera_number;
$post_fields = array("file" => new CURLFile($file_to_write), "upload_preset" => $cloudinary_upload_preset, "folder" => $folder);

$ch = curl_init();
$options = array(
    CURLOPT_URL => $cloudinary_url,
    CURLOPT_HEADER => false,
    CURLOPT_POST => true,
    CURLOPT_HTTPHEADER => $headers,
    CURLOPT_POSTFIELDS => $post_fields,
    CURLOPT_RETURNTRANSFER => true
);

curl_setopt_array($ch, $options);
$response = curl_exec($ch);

curl_close($ch);

// Get response from Cloudinary - image URL
$json = json_decode($response);
$image_public_url = $json->url;

// Write image URL to blynk image widget on pin (based on camera number)
$blynk_pin = "v1";

$blynk_auth_token = "";
if ($camera_number == "1") {
    $blynk_auth_token = $blynk_auth_token_cam1;
} elseif ($camera_number == "2") {
    $blynk_auth_token = $blynk_auth_token_cam2;
} elseif ($camera_number == "3") {
    $blynk_auth_token = $blynk_auth_token_cam3;
}

$url = "https://fra1.blynk.cloud/external/api/update/property?token=". $blynk_auth_token . "&pin=" . $blynk_pin . "&urls=" . $image_public_url;

$ch = curl_init();
$options = array(
    CURLOPT_URL => $url,
    CURLOPT_HEADER => false,
    CURLOPT_RETURNTRANSFER => true
);

curl_setopt_array($ch, $options);
$response = curl_exec($ch);
curl_close($ch);

// delete image file from directory
unlink($file_to_write);

// return response
echo $response;